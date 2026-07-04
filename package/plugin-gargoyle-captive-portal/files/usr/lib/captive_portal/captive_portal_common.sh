#!/bin/sh
# Shared helpers for the captive portal firewall include script and the
# periodic nftset-refresh cron job. Guest-AP discovery deliberately mirrors
# gargoyle_firewall_util.sh's get_guest_macs()/isolate_guest_networks() so
# both scripts agree on exactly which bridge ports are "guest".

. /lib/functions.sh

cp_guest_mac_from_uci()
{
	local is_guest_network
	local macaddr
	config_get is_guest_network "$1" is_guest_network
	if [ "$is_guest_network" = "1" ] ; then
		config_get macaddr "$1" macaddr
		echo "$macaddr"
	fi
}

cp_get_guest_macs()
{
	config_load "wireless"
	config_foreach cp_guest_mac_from_uci "wifi-iface"
}

# Resolves each guest AP's configured macaddr to its real bridge-port ifname
# on br-lan, one ifname per line. Same matching technique isolate_guest_networks()
# already uses (ip link show <port> | grep -qi <mac>), so a port only shows up
# here if the shipped guest-isolation code would also treat it as guest.
cp_get_guest_ifaces()
{
	local guest_macs
	local lanifs
	local lif
	local gmac

	guest_macs="$(cp_get_guest_macs)"
	[ -z "$guest_macs" ] && return

	lanifs="$(brctl show br-lan 2>/dev/null | awk ' $NF !~ /interfaces/ { print $NF } ')"

	for lif in $lanifs; do
		for gmac in $guest_macs; do
			if ip link show "$lif" 2>/dev/null | grep -qi "$gmac"; then
				echo "$lif"
			fi
		done
	done
}

# Union of MAC addresses currently associated to any guest AP interface.
cp_get_guest_client_macs()
{
	local iface
	for iface in $(cp_get_guest_ifaces); do
		iw dev "$iface" station dump 2>/dev/null | awk '/^Station/{print $2}'
	done | sort -u
}

# Resolves a client IPv4 address (e.g. $REMOTE_ADDR in the auth CGI, which has
# no L2 info of its own) to its MAC via the kernel neighbor table, falling
# back to /proc/net/arp if the address isn't currently cached there.
cp_mac_for_ip()
{
	local ip="$1"
	local mac

	mac="$(ip neigh show "$ip" dev br-lan 2>/dev/null \
		| awk '{for (i=1;i<=NF;i++) if ($i=="lladdr") print $(i+1)}')"

	if [ -z "$mac" ] ; then
		mac="$(awk -v ip="$ip" '$1==ip{print $4}' /proc/net/arp 2>/dev/null)"
		[ "$mac" = "00:00:00:00:00:00" ] && mac=""
	fi

	echo "$mac"
}
