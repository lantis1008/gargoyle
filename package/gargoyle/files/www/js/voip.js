/*
 * This program is copyright © 2008-2013 Eric Bishop and is distributed under the terms of the GNU GPL
 * version 2.0 with a special clarification/exception that permits adapting the program to
 * configure proprietary "back end" software provided that all modifications to the web interface
 * itself remain covered by the GPL.
 * See http://gargoyle-router.com/faq.html#qfoss for more information
 */

var voipStr = new Object(); // part of i18n

function resetData()
{
	document.getElementById("sip_alg_enabled").checked = sipAlgEnabled;
	setControlsEnabled(true);
}

function saveChanges()
{
	setControlsEnabled(false, true);

	var enabled = document.getElementById("sip_alg_enabled").checked;
	var commands = [];

	if(enabled)
	{
		// Undo a prior disable and let the firewall re-adopt the modules
		// on its next reload (fw4 only wires up a ct helper for a module
		// it finds present in /sys/module).
		commands.push("sed -i '/^blacklist nf_conntrack_sip$/d;/^blacklist nf_nat_sip$/d' /etc/modules.conf");
		commands.push("modprobe nf_conntrack_sip 2>/dev/null; true");
		commands.push("modprobe nf_nat_sip 2>/dev/null; true");
		commands.push("/etc/init.d/firewall reload >/dev/null 2>&1; true");
	}
	else
	{
		// Blacklist in /etc/modules.conf so the modules never load again -
		// nf_nat_sip/nf_conntrack_sip are also listed in the unrelated,
		// package-managed /etc/modules.d/nf-nathelper-extra, so merely
		// removing our own modules.d entry (the old approach) has no
		// effect on boot. Then strip every zone's "ct helper set sip" rule
		// and the ct helper object itself before unloading, live, without
		// a reboot: a plain firewall reload can't do this on its own,
		// since fw4 only omits the sip helper once the module is already
		// gone from /sys/module - the rule referencing it has to come
		// down first, or the module can't be removed at all.
		commands.push("grep -qs '^blacklist nf_conntrack_sip$' /etc/modules.conf || echo 'blacklist nf_conntrack_sip' >> /etc/modules.conf");
		commands.push("grep -qs '^blacklist nf_nat_sip$' /etc/modules.conf || echo 'blacklist nf_nat_sip' >> /etc/modules.conf");
		commands.push("nft -a list table inet fw4 2>/dev/null | awk '/^\\tchain /{c=$2} /ct helper set \"sip\"/{print c, $NF}' | while read chain handle; do nft delete rule inet fw4 \"$chain\" handle \"$handle\" 2>/dev/null; done");
		commands.push("nft delete ct helper inet fw4 sip 2>/dev/null; true");
		commands.push("rmmod nf_nat_sip 2>/dev/null; true");
		commands.push("rmmod nf_conntrack_sip 2>/dev/null; true");
	}

	var param = getParameterDefinition("commands", commands.join("\n")) + "&" + getParameterDefinition("hash", document.cookie.replace(/^.*hash=/, "").replace(/[\t ;]+.*$/, ""));
	var stateChangeFunction = function(req)
	{
		if(req.readyState == 4)
		{
			sipAlgEnabled = enabled;
			setControlsEnabled(true);
		}
	};
	runAjax("POST", "utility/run_commands.sh", param, stateChangeFunction);
}
