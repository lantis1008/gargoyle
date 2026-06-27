#!/usr/bin/haserl
<%
# auth.sh — captive portal authentication CGI
# Served on the guest interface only (GUEST_IP4:PORTAL_PORT).
# Not protected by the Gargoyle session validator — intentionally public.
#
# GET  /portal/auth.sh?orig=<url>  → serve index.html with injected config
# POST /portal/auth.sh             → validate password, add client to auth set

AUTH_MODE=$(uci -q get captive_portal.config.auth_mode || echo click)
GW_NAME=$(uci -q get captive_portal.config.gateway_name || echo "Guest Wi-Fi")
REDIRECT=$(uci -q get captive_portal.config.redirect_url || echo "")
CLIENT_IP="$REMOTE_ADDR"

authenticate() {
	/usr/lib/gargoyle/captive_portal.sh auth "$CLIENT_IP"
}

if [ "$REQUEST_METHOD" = "POST" ]; then
	AUTH_OK=0
	if [ "$AUTH_MODE" = "click" ]; then
		authenticate
		AUTH_OK=1
	elif [ "$AUTH_MODE" = "password" ]; then
		STORED_PW=$(uci -q get captive_portal.config.portal_password || echo "")
		if [ -n "$STORED_PW" ] && [ "$FORM_password" = "$STORED_PW" ]; then
			authenticate
			AUTH_OK=1
		fi
	fi

	if [ "$AUTH_OK" = "1" ]; then
		DEST="${FORM_orig_url:-$REDIRECT}"
		[ -z "$DEST" ] && DEST="http://www.gstatic.com/generate_204"
		echo "Status: 302 Found"
		echo "Location: $DEST"
		echo "Content-type: text/plain"
		echo ""
		exit 0
	fi
	# Fall through to show page again with error flag
	AUTH_FAILED=1
fi
%>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title><% echo "$GW_NAME" %></title>
<script>
window.PORTAL_GATEWAY_NAME = "<% echo $GW_NAME %>";
window.PORTAL_AUTH_MODE    = "<% echo $AUTH_MODE %>";
<% [ -n "$AUTH_FAILED" ] && echo 'window.PORTAL_AUTH_FAILED = true;' %>
<% [ -n "$FORM_orig_url" ] && echo "window.PORTAL_ORIG_URL = \"$FORM_orig_url\";" %>
</script>
</head>
<body>
<%
# Serve the splash page HTML inline so we only need one uhttpd path.
cat /www/portal/index.html | sed 's|<html[^>]*>||;s|</html>||;s|<!DOCTYPE[^>]*>||;s|<head>.*</head>||'
%>
</body>
</html>
