#!/usr/bin/haserl
<%
	eval $( gargoyle_session_validator -c "$COOKIE_hash" -e "$COOKIE_exp" -a "$HTTP_USER_AGENT" -i "$REMOTE_ADDR" -r "login.sh" -t $(uci get gargoyle.global.session_timeout) -b "$COOKIE_browser_time" )
	gargoyle_header_footer -h -s "firewall" -p "captive_portal" -j "captive_portal.js" -z "captive_portal.js" gargoyle captive_portal
%>

<script>
<%
	pkg="captive_portal"
	sec="config"

	echo "var enabled         = \"$(uci -q get ${pkg}.${sec}.enabled || echo 0)\";"
	echo "var gateway_name    = \"$(uci -q get ${pkg}.${sec}.gateway_name || echo 'Guest WiFi')\";"
	echo "var auth_mode       = \"$(uci -q get ${pkg}.${sec}.auth_mode || echo click)\";"
	echo "var session_timeout = \"$(uci -q get ${pkg}.${sec}.session_timeout || echo 3600)\";"
	echo "var idle_timeout    = \"$(uci -q get ${pkg}.${sec}.idle_timeout || echo 600)\";"
	echo "var max_clients     = \"$(uci -q get ${pkg}.${sec}.max_clients || echo 50)\";"
	echo "var redirect_url    = \"$(uci -q get ${pkg}.${sec}.redirect_url || echo '')\";"
	echo "var portal_port     = \"$(uci -q get ${pkg}.${sec}.portal_port || echo 2080)\";"

	# Report current authenticated client count from nftables set
	auth_count=$(nft list set inet fw4 captive_portal_auth 2>/dev/null | grep -c 'elements' || echo 0)
	echo "var auth_client_count = \"$auth_count\";"

	# Report whether guest network interface exists
	guest_if=$(ip link show br-guest 2>/dev/null && echo 1 || echo 0)
	echo "var guest_iface_exists = \"$guest_if\";"
%>
</script>

<h1 class="page-header"><%~ captive_portal.CaptivePortal %></h1>

<div class="row">
	<div class="col-lg-6">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ captive_portal.Settings %></h3>
			</div>
			<div class="panel-body">

				<div class="row form-group">
					<label class="col-xs-5 control-label" id="portal_enable_label" for="portal_enable"><%~ captive_portal.Enable %></label>
					<span class="col-xs-7">
						<input id="portal_enable" type="checkbox">
					</span>
				</div>

				<div id="portal_settings">
					<div class="row form-group">
						<label class="col-xs-5 control-label" id="gateway_name_label" for="gateway_name"><%~ captive_portal.GatewayName %></label>
						<span class="col-xs-7">
							<input id="gateway_name" class="form-control" type="text" maxlength="64">
						</span>
					</div>

					<div class="row form-group">
						<label class="col-xs-5 control-label" id="auth_mode_label"><%~ captive_portal.AuthMode %></label>
						<span class="col-xs-7">
							<select id="auth_mode" class="form-control">
								<option value="click"><%~ captive_portal.AuthClick %></option>
								<option value="password"><%~ captive_portal.AuthPassword %></option>
							</select>
						</span>
					</div>

					<div class="row form-group" id="portal_password_row">
						<label class="col-xs-5 control-label" id="portal_password_label" for="portal_password"><%~ captive_portal.Password %></label>
						<span class="col-xs-7">
							<input id="portal_password" class="form-control" type="password" maxlength="64">
						</span>
					</div>

					<div class="row form-group">
						<label class="col-xs-5 control-label" id="session_timeout_label" for="session_timeout"><%~ captive_portal.SessionTimeout %></label>
						<span class="col-xs-4">
							<input id="session_timeout" class="form-control" type="number" min="60" max="86400" step="60">
						</span>
						<span class="col-xs-3 control-label text-left"><%~ captive_portal.Seconds %></span>
					</div>

					<div class="row form-group">
						<label class="col-xs-5 control-label" id="idle_timeout_label" for="idle_timeout"><%~ captive_portal.IdleTimeout %></label>
						<span class="col-xs-4">
							<input id="idle_timeout" class="form-control" type="number" min="0" max="3600" step="60">
						</span>
						<span class="col-xs-3 control-label text-left"><%~ captive_portal.Seconds %></span>
					</div>

					<div class="row form-group">
						<label class="col-xs-5 control-label" id="max_clients_label" for="max_clients"><%~ captive_portal.MaxClients %></label>
						<span class="col-xs-7">
							<input id="max_clients" class="form-control" type="number" min="1" max="255">
						</span>
					</div>

					<div class="row form-group">
						<label class="col-xs-5 control-label" id="redirect_url_label" for="redirect_url"><%~ captive_portal.RedirectURL %></label>
						<span class="col-xs-7">
							<input id="redirect_url" class="form-control" type="url" maxlength="256" placeholder="http://">
						</span>
					</div>
				</div><!-- #portal_settings -->

				<div class="row">
					<span class="col-xs-offset-5 col-xs-7">
						<button class="btn btn-primary" onclick="saveChanges()"><%~ Save %></button>
					</span>
				</div>

			</div><!-- panel-body -->
		</div><!-- panel -->
	</div><!-- col -->

	<div class="col-lg-6">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ captive_portal.Status %></h3>
			</div>
			<div class="panel-body">
				<div class="row form-group">
					<label class="col-xs-6 control-label"><%~ captive_portal.GuestIface %></label>
					<span class="col-xs-6" id="status_iface">—</span>
				</div>
				<div class="row form-group">
					<label class="col-xs-6 control-label"><%~ captive_portal.AuthClients %></label>
					<span class="col-xs-6" id="status_clients">—</span>
				</div>
			</div>
		</div>
	</div>
</div>

<%
	gargoyle_header_footer -f -s "firewall" -p "captive_portal"
%>
