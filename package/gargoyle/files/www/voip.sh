#!/usr/bin/haserl
<%
	# This program is copyright © 2008-2013 Eric Bishop and is distributed under the terms of the GNU GPL
	# version 2.0 with a special clarification/exception that permits adapting the program to
	# configure proprietary "back end" software provided that all modifications to the web interface
	# itself remain covered by the GPL.
	# See http://gargoyle-router.com/faq.html#qfoss for more information
	eval $( gargoyle_session_validator -c "$COOKIE_hash" -e "$COOKIE_exp" -a "$HTTP_USER_AGENT" -i "$REMOTE_ADDR" -r "login.sh" -t $(uci get gargoyle.global.session_timeout) -b "$COOKIE_browser_time" )
	gargoyle_header_footer -h -s "connection" -p "voip" -j "voip.js" -z "voip.js"
%>

<script>
<!--
<%
	if grep -qsE '^(nf_nat_sip|nf_conntrack_sip)' /etc/modules.d/* 2>/dev/null ; then
		echo "var sipAlgEnabled = true;"
	else
		echo "var sipAlgEnabled = false;"
	fi

	echo "var rebootingMsg = \"<%~ voip.Rbtng %>\";"
%>
//-->
</script>

<h1 class="page-header"><%~ voip.Title %></h1>
<div class="row">
	<div class="col-lg-6">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ voip.SIPSect %></h3>
			</div>
			<div class="panel-body">
				<div class="row form-group">
					<span class="col-xs-12">
						<input type="checkbox" id="sip_alg_enabled" />
						<label for="sip_alg_enabled"><%~ voip.SIPAlgE %></label>
					</span>
				</div>
				<div class="row form-group">
					<span class="col-xs-12 text-muted small">
						<%~ voip.SIPInfo %>
					</span>
				</div>
				<div id="reboot_warning" class="row form-group" style="display:none">
					<span class="col-xs-12">
						<div class="alert alert-warning"><%~ voip.RbtWarn %></div>
					</span>
				</div>
			</div>
		</div>
	</div>
</div>

<div id="bottom_button_container" class="panel panel-default">
	<button id="save_button" class="btn btn-primary btn-lg" onclick="saveChanges()"><%~ SaveChanges %></button>
	<button id="reset_button" class="btn btn-warning btn-lg" onclick="resetData()"><%~ Reset %></button>
	<button id="reboot_button" class="btn btn-danger btn-lg" onclick="rebootNow()" style="display:none"><%~ voip.RbtNow %></button>
</div>

<script>
<!--
	resetData();
//-->
</script>

<%
	gargoyle_header_footer -f -s "connection" -p "voip"
%>
