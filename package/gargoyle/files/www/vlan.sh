#!/usr/bin/haserl
<%
	# This program is copyright © 2008-2013 Eric Bishop and is distributed under the terms of the GNU GPL
	# version 2.0 with a special clarification/exception that permits adapting the program to
	# configure proprietary "back end" software provided that all modifications to the web interface
	# itself remain covered by the GPL.
	# See http://gargoyle-router.com/faq.html#qfoss for more information
	eval $( gargoyle_session_validator -c "$COOKIE_hash" -e "$COOKIE_exp" -a "$HTTP_USER_AGENT" -i "$REMOTE_ADDR" -r "login.sh" -t $(uci get gargoyle.global.session_timeout) -b "$COOKIE_browser_time" )
	gargoyle_header_footer -h -s "connection" -p "vlan" -j "vlan.js" -z "vlan.js"
%>

<script>
<!--
<%
	# Detect hardware VLAN model: DSA vs swconfig
	if [ -e /etc/board.json ] && grep -q '"ports"' /etc/board.json 2>/dev/null ; then
		echo "var vlanHwModel = \"dsa\";"
	elif [ -e /sbin/swconfig ] ; then
		echo "var vlanHwModel = \"swconfig\";"
	else
		echo "var vlanHwModel = \"none\";"
	fi

	# Current WAN VLAN (802.1q device type or switch_wan_vlan section)
	wan_vlan_dev=$(uci show network 2>/dev/null | grep 'type=.8021q' | head -1 | cut -d. -f2)
	if [ -n "$wan_vlan_dev" ] ; then
		wan_vid=$(uci -q get network.${wan_vlan_dev}.vid)
		wan_base=$(uci -q get network.${wan_vlan_dev}.ifname)
		echo "var wanVlanActive = true;"
		echo "var wanVlanId = \"${wan_vid}\";"
		echo "var wanVlanBase = \"${wan_base}\";"
	else
		echo "var wanVlanActive = false;"
		echo "var wanVlanId = \"\";"
		echo "var wanVlanBase = \"\";"
	fi

	# Bridge VLAN filtering state (DSA)
	bridge_vlan_filter=$(uci -q get network.brlan_dev.vlan_filtering 2>/dev/null)
	echo "var bridgeVlanFiltering = \"${bridge_vlan_filter:-0}\";"

	# Enumerate existing bridge-vlan sections
	echo "var bridgeVlans = [];"
	uci show network 2>/dev/null | grep '=bridge-vlan' | cut -d= -f1 | cut -d. -f2 | while read sec ; do
		vid=$(uci -q get network.${sec}.vlan)
		ports=$(uci -q get network.${sec}.ports 2>/dev/null | tr '\n' ' ')
		[ -n "$vid" ] && printf 'bridgeVlans.push({id:"%s", ports:"%s"});\n' "$vid" "$ports"
	done

	# Enumerate network interfaces that look like VLANs (br-lan.N or wanv.N)
	echo "var vlanInterfaces = [];"
	uci show network 2>/dev/null | grep '=interface' | cut -d= -f1 | cut -d. -f2 | while read iface ; do
		dev=$(uci -q get network.${iface}.device 2>/dev/null)
		echo "$dev" | grep -qE '\.[0-9]+$' && \
			printf 'vlanInterfaces.push({name:"%s", device:"%s"});\n' "$iface" "$dev"
	done
%>
//-->
</script>

<h1 class="page-header"><%~ vlan.Title %></h1>

<div class="row">
	<div class="col-lg-6">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ vlan.StatusSect %></h3>
			</div>
			<div class="panel-body">
				<div class="row form-group">
					<label class="col-xs-5"><%~ vlan.HWModel %>:</label>
					<span class="col-xs-7" id="hw_model_label"></span>
				</div>
				<div class="row form-group">
					<label class="col-xs-5"><%~ vlan.WANVlan %>:</label>
					<span class="col-xs-7" id="wan_vlan_label"></span>
				</div>
				<div class="row form-group" id="bridge_vlan_filter_row">
					<label class="col-xs-5"><%~ vlan.BridgeFilter %>:</label>
					<span class="col-xs-7" id="bridge_vlan_filter_label"></span>
				</div>
			</div>
		</div>
	</div>

	<div class="col-lg-6" id="vlan_iface_panel" style="display:none">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ vlan.VlanIfaces %></h3>
			</div>
			<div class="panel-body">
				<div id="vlan_iface_table_container"></div>
			</div>
		</div>
	</div>
</div>

<div class="row">
	<div class="col-lg-12">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ vlan.PlannedSect %></h3>
			</div>
			<div class="panel-body">
				<div class="alert alert-info"><%~ vlan.PlannedInfo %></div>
			</div>
		</div>
	</div>
</div>

<script>
<!--
	resetData();
//-->
</script>

<%
	gargoyle_header_footer -f -s "connection" -p "vlan"
%>
