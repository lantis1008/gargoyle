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
	document.getElementById("reboot_warning").style.display = "none";
	document.getElementById("reboot_button").style.display = "none";
	setControlsEnabled(true);
}

function saveChanges()
{
	setControlsEnabled(false, true);

	var enabled = document.getElementById("sip_alg_enabled").checked;
	var commands = [];

	// Clear any existing SIP conntrack helper module entries across all modules.d files
	commands.push("for f in /etc/modules.d/*; do sed -i '/^nf_nat_sip$/d; /^nf_conntrack_sip$/d' \"$f\"; done");
	commands.push("rm -f /etc/modules.d/sip-alg");

	if(enabled)
	{
		commands.push("printf 'nf_conntrack_sip\\nnf_nat_sip\\n' > /etc/modules.d/sip-alg");
		commands.push("modprobe nf_conntrack_sip nf_nat_sip 2>/dev/null; true");
	}
	else
	{
		commands.push("modprobe -r nf_nat_sip nf_conntrack_sip 2>/dev/null; true");
	}

	var param = getParameterDefinition("commands", commands.join("\n")) + "&" + getParameterDefinition("hash", document.cookie.replace(/^.*hash=/, "").replace(/[\t ;]+.*$/, ""));
	var stateChangeFunction = function(req)
	{
		if(req.readyState == 4)
		{
			sipAlgEnabled = enabled;
			setControlsEnabled(true);
			document.getElementById("reboot_warning").style.display = "";
			document.getElementById("reboot_button").style.display = "";
		}
	};
	runAjax("POST", "utility/run_commands.sh", param, stateChangeFunction);
}

function rebootNow()
{
	document.getElementById("reboot_button").disabled = true;
	document.getElementById("save_button").disabled = true;
	document.getElementById("reset_button").disabled = true;

	var param = getParameterDefinition("commands", "(sleep 3 && reboot) &") + "&" + getParameterDefinition("hash", document.cookie.replace(/^.*hash=/, "").replace(/[\t ;]+.*$/, ""));
	var stateChangeFunction = function(req)
	{
		if(req.readyState == 4)
		{
			document.getElementById("reboot_warning").innerHTML = "<div class=\"alert alert-info\">" + rebootingMsg + "</div>";
		}
	};
	runAjax("POST", "utility/run_commands.sh", param, stateChangeFunction);
}
