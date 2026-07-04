#!/usr/bin/haserl
<%
	# Guest-facing captive portal splash page. Deliberately standalone - no
	# gargoyle_session_validator/gargoyle_header_footer here, this page is
	# served by the portal's own second uhttpd instance to unauthenticated
	# guest devices, not the admin instance.
	gateway_name=$(uci -q get captive_portal.global.gateway_name)
	[ -z "$gateway_name" ] && gateway_name="Guest WiFi"
	auth_mode=$(uci -q get captive_portal.global.auth_mode)
	[ "$auth_mode" != "password" ] && auth_mode="clickthrough"

	error="$FORM_error"

	echo "Content-type: text/html"
	echo ""
%>
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><% printf %s "$gateway_name" %></title>
<style>
	body { font-family: sans-serif; background: #f2f2f2; margin: 0; padding: 0; }
	.card { max-width: 380px; margin: 10% auto; background: #fff; border-radius: 6px;
		box-shadow: 0 1px 4px rgba(0,0,0,0.2); padding: 24px; }
	h1 { font-size: 1.3em; margin-top: 0; }
	input[type=text], input[type=password] { width: 100%; padding: 8px; margin: 6px 0 14px 0;
		box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
	button { width: 100%; padding: 10px; background: #3a7bd5; color: #fff; border: none;
		border-radius: 4px; font-size: 1em; cursor: pointer; }
	.error { color: #b00020; margin-bottom: 10px; }
</style>
</head>
<body>
<div class="card">
	<h1><% printf %s "$gateway_name" %></h1>
<%
	if [ -n "$error" ] ; then
		echo "<div class=\"error\">Incorrect password. Please try again.</div>"
	fi
%>
	<form method="post" action="auth.sh">
<%
		if [ "$auth_mode" = "password" ] ; then
			echo "<input type=\"text\" name=\"username\" placeholder=\"Username (optional)\" autocomplete=\"username\">"
			echo "<input type=\"password\" name=\"password\" placeholder=\"Password\" autocomplete=\"current-password\" required>"
		fi
%>
		<button type="submit">Connect</button>
	</form>
</div>
</body>
</html>
