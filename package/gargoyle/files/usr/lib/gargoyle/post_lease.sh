#!/bin/sh
# This program is copyright © 2026 and is distributed under the terms of the GNU GPL
# version 2.0 with a special clarification/exception that permits adapting the program to
# configure proprietary "back end" software provided that all modifications to the web interface
# itself remain covered by the GPL.
# See http://gargoyle-router.com/faq.html#qfoss for more information
#
# post_lease.sh — dnsmasq DHCP lease hook for Device Groups
#
# Registered via UCI: dhcp.@dnsmasq[0].dhcpscript=/usr/lib/gargoyle/post_lease.sh
#
# dnsmasq invokes this script with:
#   $1  event type:  add | del | old
#   $2  MAC address  (e.g. aa:bb:cc:dd:ee:ff)
#   $3  IP address   (e.g. 192.168.1.100)
#   $4  hostname     (may be '*' or empty)
#
# 'old' events are sent for existing leases at dnsmasq startup, which means
# group sets are populated automatically whenever dnsmasq (re)starts.

. /usr/lib/gargoyle/known_devices.sh

NFT_FAMILY="inet"
NFT_TABLE="fw4"

event="$1"
mac="$(printf '%s' "$2" | tr 'a-z' 'A-Z')"
ip="$3"

# Sanity checks
[ -z "$event" ] || [ -z "$mac" ] || [ -z "$ip" ] && exit 0
[ "$ip" = "*" ] && exit 0
[ "$event" != "add" ] && [ "$event" != "del" ] && [ "$event" != "old" ] && exit 0

# Find which group this MAC belongs to by scanning UCI host sections
for section in $(get_all_known_device_sections); do
	stored_mac=$(get_device_field "$section" "mac" | tr 'a-z' 'A-Z')
	[ "$stored_mac" != "$mac" ] && continue

	group=$(get_device_group "$section")
	[ -z "$group" ] && exit 0

	setname=$(group_to_set_name "$group")

	if [ "$event" = "add" ] || [ "$event" = "old" ]; then
		nft add element "$NFT_FAMILY" "$NFT_TABLE" "$setname" \{ "$ip" \} 2>/dev/null
	else
		nft delete element "$NFT_FAMILY" "$NFT_TABLE" "$setname" \{ "$ip" \} 2>/dev/null
	fi

	exit 0
done

exit 0
