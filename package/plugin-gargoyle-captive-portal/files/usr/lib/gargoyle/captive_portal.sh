#!/bin/sh
# captive_portal.sh — nftables-based captive portal backend for Gargoyle
#
# Called with one argument:
#   setup    — apply rules (called from firewall hotplug / after fw4 restart)
#   teardown — remove all portal rules and sets
#   auth     — mark a client as authenticated:  captive_portal.sh auth <ip>
#   deauth   — remove a client's authentication: captive_portal.sh deauth <ip>
#
# Architecture (Option D — nftables DIY, no external packages):
#
#   Guest SSID → br-guest (192.168.2.0/24, separate DHCP pool)
#     │
#     ├─ Unauthenticated client:
#     │    HTTP (port 80) → DNAT redirect → router:$PORTAL_PORT (uhttpd)
#     │                                     → /www/portal/index.html (splash)
#     │    All other outbound traffic → DROP
#     │
#     └─ Authenticated client (IP in captive_portal_auth set):
#          All traffic → ACCEPT (normal internet access)
#
# To swap in openNDS/nodogsplash (Option A/B), replace the nftables block in
# setup_portal() with the appropriate daemon start and remove the redirect rules.
# The UCI config and Gargoyle UI remain identical across all options.

PORTAL_TABLE="inet fw4"
AUTH_SET="captive_portal_auth"
GUEST_IFACE="br-guest"
GUEST_IP4="192.168.2.1"
GUEST_SUBNET="192.168.2.0/24"
LOCK="/var/lock/captive_portal.lock"

cfg_get() { uci -q get "captive_portal.config.$1"; }

portal_port()        { cfg_get portal_port    || echo 2080; }
session_timeout()    { cfg_get session_timeout || echo 3600; }

# --------------------------------------------------------------------------
# nftables helpers
# --------------------------------------------------------------------------

set_exists() {
	nft list set $PORTAL_TABLE "$AUTH_SET" >/dev/null 2>&1
}

create_auth_set() {
	local timeout
	timeout=$(session_timeout)
	set_exists && return 0
	nft add set $PORTAL_TABLE "$AUTH_SET" \
		"{ type ipv4_addr; flags timeout; timeout ${timeout}s; }"
}

delete_auth_set() {
	set_exists && nft delete set $PORTAL_TABLE "$AUTH_SET" 2>/dev/null || true
}

# Remove any rules we previously injected into fw4 chains (identified by comment)
flush_portal_rules() {
	# Delete rules with our comment tag from relevant chains
	for chain in dstnat_lan forward_lan; do
		nft -a list chain $PORTAL_TABLE "$chain" 2>/dev/null \
			| awk '/captive_portal/ { print $NF }' \
			| while read handle; do
				nft delete rule $PORTAL_TABLE "$chain" handle "$handle" 2>/dev/null || true
			done
	done
	# Remove our dedicated chains if present
	nft delete chain $PORTAL_TABLE captive_portal_dstnat 2>/dev/null || true
	nft delete chain $PORTAL_TABLE captive_portal_forward 2>/dev/null || true
}

setup_portal() {
	local port
	port=$(portal_port)

	# Flush any previous rules so restarts are idempotent
	flush_portal_rules
	create_auth_set

	# -- DSTNAT chain: redirect unauthenticated guest HTTP to the portal ------
	nft add chain $PORTAL_TABLE captive_portal_dstnat \
		"{ comment \"captive_portal\"; }"
	# Authenticated clients bypass redirect
	nft add rule  $PORTAL_TABLE captive_portal_dstnat \
		"iifname \"$GUEST_IFACE\" ip saddr @${AUTH_SET} return comment \"captive_portal\""
	# Redirect HTTP to portal page
	nft add rule  $PORTAL_TABLE captive_portal_dstnat \
		"iifname \"$GUEST_IFACE\" tcp dport 80 redirect to :${port} comment \"captive_portal\""

	# Hook chain into fw4's dstnat (insert before existing rules)
	nft insert rule $PORTAL_TABLE dstnat \
		"iifname \"$GUEST_IFACE\" jump captive_portal_dstnat comment \"captive_portal\""

	# -- FORWARD chain: block unauthenticated guest internet access -----------
	nft add chain $PORTAL_TABLE captive_portal_forward \
		"{ comment \"captive_portal\"; }"
	# Allow DNS + DHCP so clients can get addresses and reach the portal
	nft add rule  $PORTAL_TABLE captive_portal_forward \
		"iifname \"$GUEST_IFACE\" udp dport { 53, 67 } accept comment \"captive_portal\""
	# Allow portal traffic (to the router itself, not forwarded)
	# Authenticated clients get full internet access
	nft add rule  $PORTAL_TABLE captive_portal_forward \
		"iifname \"$GUEST_IFACE\" ip saddr @${AUTH_SET} accept comment \"captive_portal\""
	# Drop everything else from unauthenticated guests
	nft add rule  $PORTAL_TABLE captive_portal_forward \
		"iifname \"$GUEST_IFACE\" drop comment \"captive_portal\""

	nft insert rule $PORTAL_TABLE forward \
		"iifname \"$GUEST_IFACE\" jump captive_portal_forward comment \"captive_portal\""

	# -- Configure uhttpd to serve the portal on the guest interface ----------
	# We run a second uhttpd instance on GUEST_IP4:$port to serve the splash
	# page, so it doesn't conflict with the admin UI on port 80.
	start_portal_httpd "$port"
}

teardown_portal() {
	flush_portal_rules
	delete_auth_set
	stop_portal_httpd
}

# --------------------------------------------------------------------------
# Portal HTTP server (splash page)
# Serve /www/portal/ on the guest interface at the redirect port.
# uhttpd is already on the router; we spawn a second instance.
# --------------------------------------------------------------------------

HTTPD_PID="/var/run/captive_portal_httpd.pid"

start_portal_httpd() {
	local port="$1"
	stop_portal_httpd
	uhttpd \
		-p "${GUEST_IP4}:${port}" \
		-h /www/portal \
		-f \
		-P "$HTTPD_PID" \
		-T 30 \
		-n 3 \
		>/dev/null 2>&1 &
}

stop_portal_httpd() {
	if [ -f "$HTTPD_PID" ]; then
		kill "$(cat $HTTPD_PID)" 2>/dev/null || true
		rm -f "$HTTPD_PID"
	fi
	# Belt-and-suspenders: kill any leftover instances bound to our port
	local port
	port=$(portal_port)
	fuser -k "${port}/tcp" 2>/dev/null || true
}

# --------------------------------------------------------------------------
# Client authentication — called by the portal CGI on form submit
# --------------------------------------------------------------------------

auth_client() {
	local ip="$1"
	[ -z "$ip" ] && { echo "Usage: $0 auth <ip>"; exit 1; }
	local timeout
	timeout=$(session_timeout)
	set_exists || create_auth_set
	nft add element $PORTAL_TABLE "$AUTH_SET" "{ $ip timeout ${timeout}s }"
	logger -t captive_portal "authenticated: $ip (timeout: ${timeout}s)"
}

deauth_client() {
	local ip="$1"
	[ -z "$ip" ] && { echo "Usage: $0 deauth <ip>"; exit 1; }
	set_exists && nft delete element $PORTAL_TABLE "$AUTH_SET" "{ $ip }" 2>/dev/null || true
	logger -t captive_portal "deauthenticated: $ip"
}

# --------------------------------------------------------------------------
# Guest network UCI setup
# Idempotent — safe to call on every firewall restart.
# Creates the 'guest' network zone if it doesn't already exist.
# --------------------------------------------------------------------------

setup_guest_network() {
	# Network interface
	if ! uci -q get network.guest >/dev/null; then
		uci set network.guest='interface'
		uci set network.guest.type='bridge'
		uci set network.guest.proto='static'
		uci set network.guest.ipaddr="$GUEST_IP4"
		uci set network.guest.netmask='255.255.255.0'
	fi

	# DHCP pool for guest network
	if ! uci -q get dhcp.guest >/dev/null; then
		uci set dhcp.guest='dhcp'
		uci set dhcp.guest.interface='guest'
		uci set dhcp.guest.start='100'
		uci set dhcp.guest.limit='50'
		uci set dhcp.guest.leasetime='1h'
	fi

	# Firewall zone for guest network
	local guest_zone
	guest_zone=$(uci show firewall | awk -F= '/\.name=.guest./{sub(/\.name.*/,""); print $1}')
	if [ -z "$guest_zone" ]; then
		local idx
		idx=$(uci add firewall zone)
		uci set "firewall.${idx}.name=guest"
		uci set "firewall.${idx}.network=guest"
		uci set "firewall.${idx}.input=REJECT"
		uci set "firewall.${idx}.output=ACCEPT"
		uci set "firewall.${idx}.forward=REJECT"
	fi

	# Allow DNS and DHCP inbound on guest zone
	if ! uci show firewall | grep -q 'captive_portal_dns'; then
		local r
		r=$(uci add firewall rule)
		uci set "firewall.${r}.name=captive_portal_dns"
		uci set "firewall.${r}.src=guest"
		uci set "firewall.${r}.dest_port=53"
		uci set "firewall.${r}.target=ACCEPT"
		r=$(uci add firewall rule)
		uci set "firewall.${r}.name=captive_portal_dhcp"
		uci set "firewall.${r}.src=guest"
		uci set "firewall.${r}.dest_port=67"
		uci set "firewall.${r}.proto=udp"
		uci set "firewall.${r}.target=ACCEPT"
		# Allow access to portal page on PORTAL_PORT
		r=$(uci add firewall rule)
		uci set "firewall.${r}.name=captive_portal_http"
		uci set "firewall.${r}.src=guest"
		uci set "firewall.${r}.dest_port=$(portal_port)"
		uci set "firewall.${r}.proto=tcp"
		uci set "firewall.${r}.target=ACCEPT"
	fi

	uci commit network
	uci commit dhcp
	uci commit firewall
}

# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------

(
flock -x 200

case "$1" in
	setup)
		enabled=$(cfg_get enabled)
		if [ "$enabled" = "1" ]; then
			setup_guest_network
			setup_portal
		else
			teardown_portal
		fi
		;;
	teardown)
		teardown_portal
		;;
	auth)
		auth_client "$2"
		;;
	deauth)
		deauth_client "$2"
		;;
	*)
		echo "Usage: $0 {setup|teardown|auth <ip>|deauth <ip>}"
		exit 1
		;;
esac

) 200>"$LOCK"
