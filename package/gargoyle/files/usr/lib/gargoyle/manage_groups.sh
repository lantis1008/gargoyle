#!/bin/sh
# This program is copyright © 2026 and is distributed under the terms of the GNU GPL
# version 2.0 with a special clarification/exception that permits adapting the program to
# configure proprietary "back end" software provided that all modifications to the web interface
# itself remain covered by the GPL.
# See http://gargoyle-router.com/faq.html#qfoss for more information
#
# manage_groups.sh — Device Group nftables set management
#
# Maintains one nftables set per Device Group in inet fw4.
# Called from ifup_firewall() after fw4 initialises, so the table is guaranteed
# to exist.  Safe to call multiple times (idempotent).
#
# Set lifecycle:
#   - Sets for groups that exist in UCI are (re)created and seeded from
#     /tmp/dhcp.leases so IPs are correct even if dnsmasq is already running.
#   - Sets whose group name has been removed from UCI are deleted.
#
# Runtime updates are handled by post_lease.sh via the dnsmasq dhcpscript hook.

. /usr/lib/gargoyle/known_devices.sh

NFT_FAMILY="inet"
NFT_TABLE="fw4"

# Guard: do nothing if fw4 table isn't up yet
nft list table "$NFT_FAMILY" "$NFT_TABLE" >/dev/null 2>&1 || exit 0

groups=$(get_all_groups)

# Build a space-separated list of desired set names
desired_sets=""
for group in $groups; do
	desired_sets="$desired_sets $(group_to_set_name "$group")"
done

# Remove sets whose group no longer exists in UCI
for existing in $(nft list sets "$NFT_FAMILY" "$NFT_TABLE" 2>/dev/null | awk '/set grp_/{print $2}'); do
	found=0
	for wanted in $desired_sets; do
		[ "$existing" = "$wanted" ] && found=1 && break
	done
	[ "$found" = "0" ] && nft delete set "$NFT_FAMILY" "$NFT_TABLE" "$existing" 2>/dev/null
done

# Create/recreate each group's set and seed it from the current lease table
for group in $groups; do
	setname=$(group_to_set_name "$group")

	# Recreate to flush stale IPs (rules referencing this set survive deletion
	# in nftables if we add it back before the transaction commits, but since
	# restore_quotas/make_nftables_rules recreate rules anyway this is fine)
	nft delete set "$NFT_FAMILY" "$NFT_TABLE" "$setname" 2>/dev/null
	nft add set "$NFT_FAMILY" "$NFT_TABLE" "$setname" \{ type ipv4_addr\; \} 2>/dev/null || continue

	# Seed from /tmp/dhcp.leases (format: <expiry> <mac> <ip> <name> <clientid>)
	for mac in $(get_macs_in_group "$group"); do
		lc_mac=$(printf '%s' "$mac" | tr 'A-Z' 'a-z')
		ip=$(awk -v m="$lc_mac" 'tolower($2)==m && $3!="*" {print $3; exit}' /tmp/dhcp.leases 2>/dev/null)
		[ -n "$ip" ] && nft add element "$NFT_FAMILY" "$NFT_TABLE" "$setname" \{ "$ip" \} 2>/dev/null
	done
done
