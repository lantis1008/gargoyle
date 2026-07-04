#!/usr/bin/haserl
<%
	eval $( gargoyle_session_validator -c "$COOKIE_hash" -e "$COOKIE_exp" -a "$HTTP_USER_AGENT" -i "$REMOTE_ADDR" -r "login.sh" -t $(uci get gargoyle.global.session_timeout) -b "$COOKIE_browser_time" )
	gargoyle_header_footer -h -s "firewall" -p "captive_portal" -j "captive_portal.js" -z "captive_portal.js" gargoyle captive_portal
%>

<script>
<!--
<%
	guest_count=$(nft list set inet fw4 captive_portal_guest_macs 2>/dev/null | grep -c ":")
	auth_count=$(nft list set inet fw4 captive_portal_auth 2>/dev/null | grep -c "\.")
	echo "var cpGuestCount = ${guest_count:-0};"
	echo "var cpAuthCount = ${auth_count:-0};"
%>
//-->
</script>

<h1 class="page-header"><%~ captive_portal.Title %></h1>
<div class="row">
	<div class="col-lg-6">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ captive_portal.Sect %></h3>
			</div>
			<div class="panel-body">
				<div class="row form-group">
					<span class="col-xs-12">
						<input type="checkbox" id="cp_enabled" />
						<label for="cp_enabled"><%~ captive_portal.Enable %></label>
					</span>
				</div>

				<div class="row form-group">
					<label class="col-xs-5" for="cp_gateway_name"><%~ captive_portal.GatewayName %>:</label>
					<span class="col-xs-7"><input type="text" id="cp_gateway_name" class="form-control" /></span>
				</div>

				<div class="row form-group">
					<label class="col-xs-5" for="cp_auth_mode"><%~ captive_portal.AuthMode %>:</label>
					<span class="col-xs-7">
						<select id="cp_auth_mode" class="form-control" onchange="updateAuthModeVisibility()">
							<option value="clickthrough"><%~ captive_portal.AuthClickthrough %></option>
							<option value="password"><%~ captive_portal.AuthPassword %></option>
						</select>
					</span>
				</div>

				<div class="row form-group" id="cp_password_row">
					<label class="col-xs-5" for="cp_new_password"><%~ captive_portal.Password %>:</label>
					<span class="col-xs-7"><input type="password" id="cp_new_password" class="form-control" autocomplete="new-password" /></span>
				</div>
				<div class="row form-group">
					<span class="col-xs-12 text-muted small"><%~ captive_portal.PasswordHelp %></span>
				</div>

				<div class="row form-group">
					<label class="col-xs-5" for="cp_session_minutes"><%~ captive_portal.SessionMinutes %>:</label>
					<span class="col-xs-7"><input type="text" id="cp_session_minutes" class="form-control" /></span>
				</div>

				<div class="row form-group">
					<label class="col-xs-5" for="cp_port"><%~ captive_portal.Port %>:</label>
					<span class="col-xs-7"><input type="text" id="cp_port" class="form-control" /></span>
				</div>

				<div class="row form-group">
					<label class="col-xs-5" for="cp_redirect_url"><%~ captive_portal.RedirectURL %>:</label>
					<span class="col-xs-7"><input type="text" id="cp_redirect_url" class="form-control" /></span>
				</div>
			</div>
		</div>
	</div>

	<div class="col-lg-6">
		<div class="panel panel-default">
			<div class="panel-heading">
				<h3 class="panel-title"><%~ captive_portal.StatusSect %></h3>
			</div>
			<div class="panel-body">
				<div class="row form-group">
					<label class="col-xs-8"><%~ captive_portal.GuestCount %>:</label>
					<span class="col-xs-4" id="cp_guest_count"></span>
				</div>
				<div class="row form-group">
					<label class="col-xs-8"><%~ captive_portal.AuthCount %>:</label>
					<span class="col-xs-4" id="cp_auth_count"></span>
				</div>
			</div>
		</div>
	</div>
</div>

<div id="bottom_button_container" class="panel panel-default">
	<button id="save_button" class="btn btn-primary btn-lg" onclick="saveChanges()"><%~ SaveChanges %></button>
	<button id="reset_button" class="btn btn-warning btn-lg" onclick="resetData()"><%~ Reset %></button>
</div>

<script>
<!--
	resetData();
	document.getElementById('cp_guest_count').innerHTML = cpGuestCount;
	document.getElementById('cp_auth_count').innerHTML = cpAuthCount;
//-->
</script>

<%
	gargoyle_header_footer -f -s "firewall" -p "captive_portal"
%>
