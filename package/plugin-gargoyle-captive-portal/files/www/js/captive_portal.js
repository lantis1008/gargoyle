/*
 * captive_portal.js — Gargoyle captive portal plugin UI
 */
var captive_portal = new Object(); // i18n namespace

var pkg = "captive_portal";
var sec = "config";

function resetData()
{
	document.getElementById("portal_enable").checked = (enabled == "1");
	document.getElementById("gateway_name").value    = gateway_name;
	document.getElementById("session_timeout").value = session_timeout;
	document.getElementById("idle_timeout").value    = idle_timeout;
	document.getElementById("max_clients").value     = max_clients;
	document.getElementById("redirect_url").value    = redirect_url;

	setSelectedValue("auth_mode", auth_mode);
	updateAuthModeVisibility();
	updateSettingsVisibility();
	updateStatus();

	uciOriginal.removeSection("gargoyle", "help");
}

function updateAuthModeVisibility()
{
	var mode = getSelectedValue("auth_mode");
	document.getElementById("portal_password_row").style.display =
		(mode === "password") ? "" : "none";
}

function updateSettingsVisibility()
{
	var enabled = document.getElementById("portal_enable").checked;
	document.getElementById("portal_settings").style.display = enabled ? "" : "none";
}

function updateStatus()
{
	var ifaceEl   = document.getElementById("status_iface");
	var clientsEl = document.getElementById("status_clients");

	if (guest_iface_exists === "1")
	{
		ifaceEl.textContent   = "br-guest (192.168.2.1)";
		clientsEl.textContent = auth_client_count;
	}
	else
	{
		ifaceEl.textContent   = captive_portal.IfaceNotReady || "(not configured)";
		clientsEl.textContent = "—";
	}
}

function proofreadData()
{
	var errors = [];
	var name = document.getElementById("gateway_name").value.trim();
	if (!name)
	{
		errors.push(captive_portal.ErrGwName || "Gateway name cannot be empty.");
	}
	var sess = parseInt(document.getElementById("session_timeout").value, 10);
	if (isNaN(sess) || sess < 60)
	{
		errors.push(captive_portal.ErrSessionTimeout || "Session timeout must be at least 60 seconds.");
	}
	var mode = getSelectedValue("auth_mode");
	if (mode === "password")
	{
		var pw = document.getElementById("portal_password").value;
		if (!pw)
		{
			errors.push(captive_portal.ErrPassword || "A password is required when using password auth mode.");
		}
	}
	return errors;
}

function saveChanges()
{
	var errors = proofreadData();
	if (errors.length > 0)
	{
		alert(errors.join("\n"));
		return;
	}

	setControlsEnabled(false, true);

	var isEnabled = document.getElementById("portal_enable").checked ? "1" : "0";
	var name      = document.getElementById("gateway_name").value.trim();
	var mode      = getSelectedValue("auth_mode");
	var pw        = document.getElementById("portal_password").value;
	var sess      = document.getElementById("session_timeout").value;
	var idle      = document.getElementById("idle_timeout").value;
	var maxc      = document.getElementById("max_clients").value;
	var redir     = document.getElementById("redirect_url").value.trim();

	uci.set(pkg, sec, "enabled",         isEnabled);
	uci.set(pkg, sec, "gateway_name",    name);
	uci.set(pkg, sec, "auth_mode",       mode);
	uci.set(pkg, sec, "session_timeout", sess);
	uci.set(pkg, sec, "idle_timeout",    idle);
	uci.set(pkg, sec, "max_clients",     maxc);
	uci.set(pkg, sec, "redirect_url",    redir);
	if (mode === "password" && pw)
	{
		uci.set(pkg, sec, "portal_password", pw);
	}

	var commands = uci.getScriptCommands(uciOriginal)
		+ "\nuci commit " + pkg
		+ "\nsh /usr/lib/gargoyle/captive_portal.sh setup";

	var param = getParameterDefinition("commands", commands)
		+ "&" + getParameterDefinition("hash",
			document.cookie.replace(/^.*hash=/, "").replace(/[\t ;]+.*$/, ""));

	var stateChangeFunction = function(req)
	{
		if (req.readyState == 4)
		{
			setControlsEnabled(true);
			window.location.href = window.location.href;
		}
	};

	runAjax("POST", "utility/run_commands.sh", param, stateChangeFunction);
}

document.addEventListener("change", function(e)
{
	if (e.target.id === "portal_enable") { updateSettingsVisibility(); }
	if (e.target.id === "auth_mode")     { updateAuthModeVisibility(); }
});
