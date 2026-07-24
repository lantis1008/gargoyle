#!/usr/bin/haserl
<? 
	# This program is copyright © 2008 Eric Bishop and is distributed under the terms of the GNU GPL
	# version 2.0 with a special clarification/exception that permits adapting the program to
	# configure proprietary "back end" software provided that all modifications to the web interface
	# itself remain covered by the GPL.
	# See http://gargoyle-router.com/faq.html#qfoss for more information
	eval $( gargoyle_session_validator -c "$POST_hash" -e "$COOKIE_exp" -a "$HTTP_USER_AGENT" -i "$REMOTE_ADDR" -r "login.sh" -t $(uci get gargoyle.global.session_timeout) -b "$COOKIE_browser_time"  )

	echo "Content-type: text/plain"
	echo ""

	if [ -n "$FORM_commands" ] ; then

		# Unique temp file per request (mktemp) so a concurrent save from
		# another tab can't overwrite this request's command file before it
		# runs, and an exclusive lock around execution so two requests'
		# `uci commit`s can't interleave and corrupt a shared config file.
		# Both symptoms surface on the forum as a service (dnsmasq/firewall/
		# qos) crashing after a multi-tab save. The mktemp template keeps the
		# X's at the very end -- busybox mktemp rejects a suffix after them.
		tmp_file=$(mktemp /tmp/gargoyle_cmd.XXXXXX)
		if [ -n "$tmp_file" ] ; then
			printf "%s" "$FORM_commands" | tr -d "\r" > "$tmp_file"
			(
				flock -x 200
				sh "$tmp_file"
			) 200>/var/lock/gargoyle_uci.lock
			rm -f "$tmp_file"
		fi
	fi
	echo "Success"
?>
