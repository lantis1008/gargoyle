#!/bin/sh
# This program is copyright © 2026 and is distributed under the terms of the GNU GPL
# version 2.0 with a special clarification/exception that permits adapting the program to
# configure proprietary "back end" software provided that all modifications to the web interface
# itself remain covered by the GPL.
# See http://gargoyle-router.com/faq.html#qfoss for more information
#
# Known Devices / Device Groups — shared UCI helper library
#
# Data model (stored in /etc/config/dhcp):
#
#   config host 'static_host_1'
#       option name    'Johns-Laptop'
#       option mac     'AA:BB:CC:DD:EE:FF'
#       option ip      '192.168.1.100'   # optional static IP
#       option group   'family'          # optional group membership
#
# Groups are identified purely by their name string — there is no separate
# 'config group' section.  All unique group values across all host sections
# are the set of defined groups.
#
# nftables set naming:
#   Group names are sanitized to lowercase, with any character outside
#   [a-z0-9_] replaced by '_', and prefixed with 'grp_' to avoid collisions
#   with other nftables objects.  Names are truncated to 31 characters total
#   (nftables identifier limit).
#   e.g.  "Family Devices" -> "grp_family_devices"
#         "Dad's Phone!"   -> "grp_dad_s_phone_"


# group_to_set_name <group_name>
# Converts a human-readable group name to a safe nftables set name.
group_to_set_name()
{
	printf 'grp_%s' "$(printf '%s' "$1" | tr 'A-Z' 'a-z' | tr -cs 'a-z0-9_' '_')" | cut -c1-31
}

# get_all_known_device_sections
# Prints UCI section names for every 'host' section in /etc/config/dhcp.
get_all_known_device_sections()
{
	uci show dhcp 2>/dev/null | grep '=host$' | sed 's/dhcp\.\(.*\)=host/\1/'
}

# get_device_field <section> <field>
# Returns the value of a field for a host section, or empty string if unset.
get_device_field()
{
	uci get "dhcp.$1.$2" 2>/dev/null
}

# get_device_group <section>
# Returns the group name for a host section, or empty string if unset.
get_device_group()
{
	get_device_field "$1" "group"
}

# set_device_group <section> <group_name>
# Assigns a host section to a group.  Pass empty string to remove.
set_device_group()
{
	local section="$1"
	local group="$2"
	if [ -z "$group" ]
	then
		uci del "dhcp.$section.group" 2>/dev/null
	else
		uci set "dhcp.$section.group=$group"
	fi
}

# get_all_groups
# Prints each unique group name (one per line) found across all host sections.
get_all_groups()
{
	local sections
	sections=$(get_all_known_device_sections)
	local section
	for section in $sections
	do
		get_device_group "$section"
	done | sort -u | grep -v '^$'
}

# get_sections_in_group <group_name>
# Prints the UCI section names of every host that belongs to the given group.
get_sections_in_group()
{
	local target_group="$1"
	local sections
	sections=$(get_all_known_device_sections)
	local section
	for section in $sections
	do
		local grp
		grp=$(get_device_group "$section")
		if [ "$grp" = "$target_group" ]
		then
			echo "$section"
		fi
	done
}

# get_macs_in_group <group_name>
# Prints the MAC address of every host that belongs to the given group.
get_macs_in_group()
{
	local sections
	sections=$(get_sections_in_group "$1")
	local section
	for section in $sections
	do
		get_device_field "$section" "mac"
	done | grep -v '^$'
}
