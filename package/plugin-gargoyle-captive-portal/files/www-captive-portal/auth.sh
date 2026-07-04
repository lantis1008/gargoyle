#!/usr/bin/haserl
<%
	# Guest-facing captive portal auth handler (POST). Deliberately standalone -
	# no gargoyle_session_validator here, this page is served by the portal's
	# own second uhttpd instance to unauthenticated guest devices.
	#
	# auth_mode is read from UCI, never trusted from the submitted form - a
	# guest client must not be able to choose "clickthrough" for itself when
	# the admin configured password auth.
	. /usr/lib/captive_portal/captive_portal_common.sh

	auth_mode=$(uci -q get captive_portal.global.auth_mode)
	[ "$auth_mode" != "password" ] && auth_mode="clickthrough"
	session_minutes=$(uci -q get captive_portal.global.session_minutes)
	case "$session_minutes" in
		''|*[!0-9]*) session_minutes=60 ;;
	esac
	redirect_url=$(uci -q get captive_portal.global.redirect_url)
	[ -z "$redirect_url" ] && redirect_url="http://www.gstatic.com/generate_204"

	client_ip="$REMOTE_ADDR"
	client_mac="$(cp_mac_for_ip "$client_ip")"

	authorized="0"
	if [ -n "$client_mac" ] ; then
		if [ "$auth_mode" = "password" ] ; then
			stored_hash=$(uci -q get captive_portal.auth.password_hash)
			if [ -n "$stored_hash" ] && /usr/sbin/captive_portal_passwd -V "$FORM_password" "$stored_hash" ; then
				authorized="1"
			fi
		else
			authorized="1"
		fi
	fi

	if [ "$authorized" = "1" ] ; then
		nft add element inet fw4 captive_portal_auth \{ "$client_ip" . "$client_mac" timeout "${session_minutes}m" \} 2>/dev/null
		logger -t captive_portal "authorized ${client_ip} (${client_mac})"
		echo "Status: 302 Found"
		echo "Location: ${redirect_url}"
		echo ""
		echo ""
	else
		echo "Status: 302 Found"
		echo "Location: index.sh?error=1"
		echo ""
		echo ""
	fi
%>
