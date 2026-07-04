var captive_portal = new Object(); //part of i18n

var uci = uciOriginal.clone();

function resetData()
{
	uci = uciOriginal.clone();

	var enabled = uci.get('captive_portal', 'global', 'enabled') == '1';
	document.getElementById('cp_enabled').checked = enabled;

	var authMode = uci.get('captive_portal', 'global', 'auth_mode');
	if (authMode != 'clickthrough' && authMode != 'password')
	{
		authMode = 'clickthrough';
	}
	document.getElementById('cp_auth_mode').value = authMode;

	document.getElementById('cp_gateway_name').value = uci.get('captive_portal', 'global', 'gateway_name');
	document.getElementById('cp_session_minutes').value = uci.get('captive_portal', 'global', 'session_minutes');
	document.getElementById('cp_port').value = uci.get('captive_portal', 'global', 'port');
	document.getElementById('cp_redirect_url').value = uci.get('captive_portal', 'global', 'redirect_url');

	// Password is write-only - never pre-fill from the stored hash. Leaving
	// this blank on save means "keep the existing password unchanged".
	document.getElementById('cp_new_password').value = '';

	updateAuthModeVisibility();
}

function updateAuthModeVisibility()
{
	var isPassword = document.getElementById('cp_auth_mode').value == 'password';
	document.getElementById('cp_password_row').style.display = isPassword ? '' : 'none';
}

function proofreadData()
{
	var errors = new Array();

	var port = document.getElementById('cp_port').value;
	if (!/^[0-9]+$/.test(port) || port < 1 || port > 65535)
	{
		errors.push(captive_portal.ErrPort);
	}

	var sessionMinutes = document.getElementById('cp_session_minutes').value;
	if (!/^[0-9]+$/.test(sessionMinutes) || sessionMinutes < 1)
	{
		errors.push(captive_portal.ErrSession);
	}

	if (document.getElementById('cp_auth_mode').value == 'password')
	{
		var newPassword = document.getElementById('cp_new_password').value;
		var haveExistingHash = uciOriginal.get('captive_portal', 'auth', 'password_hash').length > 0;
		if (newPassword.length == 0 && !haveExistingHash)
		{
			errors.push(captive_portal.ErrNoPassword);
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

	uci.set('captive_portal', 'global', 'enabled', document.getElementById('cp_enabled').checked ? '1' : '0');
	uci.set('captive_portal', 'global', 'auth_mode', document.getElementById('cp_auth_mode').value);
	uci.set('captive_portal', 'global', 'gateway_name', document.getElementById('cp_gateway_name').value);
	uci.set('captive_portal', 'global', 'session_minutes', document.getElementById('cp_session_minutes').value);
	uci.set('captive_portal', 'global', 'port', document.getElementById('cp_port').value);
	uci.set('captive_portal', 'global', 'redirect_url', document.getElementById('cp_redirect_url').value);

	var commands = uci.getScriptCommands(uciOriginal);

	var newPassword = document.getElementById('cp_new_password').value;
	if (newPassword.length > 0)
	{
		var escaped = newPassword.replace(/'/g, "'\\''");
		commands += "\nNEW_CP_HASH=\"$(/usr/sbin/captive_portal_passwd -H '" + escaped + "')\"\n";
		commands += "uci set captive_portal.auth.password_hash=\"$NEW_CP_HASH\"\n";
	}

	commands += "uci commit captive_portal\n";

	var port = document.getElementById('cp_port').value;
	commands += "uci set uhttpd.captive_portal.listen_http='0.0.0.0:" + port + "'\n";
	commands += "uci commit uhttpd\n";
	commands += "/etc/init.d/uhttpd restart\n";
	commands += "sh /etc/captive_portal.firewall restart\n";

	var param = "restore_defaults=false&commands=" + encodeURIComponent(commands);

	var stateChangeFunction = function(ajax)
	{
		if (ajax.readyState == 4)
		{
			if (ajax.status == 200)
			{
				uciOriginal = uci.clone();
				resetData();
			}
		}
	};
	runAjax("POST", "utility/run_commands.sh", param, stateChangeFunction);
}
