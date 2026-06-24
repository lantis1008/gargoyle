/*
 * This program is copyright © 2008-2013 Eric Bishop and is distributed under the terms of the GNU GPL
 * version 2.0 with a special clarification/exception that permits adapting the program to
 * configure proprietary "back end" software provided that all modifications to the web interface
 * itself remain covered by the GPL.
 * See http://gargoyle-router.com/faq.html#qfoss for more information
 */

var dhcpS=new Object(); //part of i18n
var TSort_Data = new Array ('devices_table', 's', 's', 'p', 's', 's', 's', '');

function saveChanges()
{
	errorList = proofreadAll();
	if(errorList.length > 0)
	{
		errorString = errorList.join("\n") + "\n\n"+UI.ErrChanges;
		alert(errorString);
	}
	else
	{
		setControlsEnabled(false, true);

		var staticHostCommands = [];
		var staticHostSections = uciOriginal.getAllSectionsOfType("dhcp", "host");
		while(staticHostSections.length > 0)
		{
			var lastSection = staticHostSections.pop();
			uciOriginal.removeSection("dhcp", lastSection);
			staticHostCommands.push("uci del dhcp." + lastSection);
		}
		uci = uciOriginal.clone();
		uci.remove('dhcp', dhcpSection, 'ignore');
		uci.set('dhcp', dhcpSection, 'interface', 'lan');
		dhcpIds =  ['dhcp_start', ['dhcp_start','dhcp_end'], 'dhcp_lease'];
		dhcpVisIds = ['dhcp_start', 'dhcp_end', 'dhcp_lease'];
		dhcpPkgs = ['dhcp','dhcp','dhcp'];
		dhcpSections = [dhcpSection,dhcpSection,dhcpSection];
		dhcpOptions = ['start', 'limit', 'leasetime'];

		dhcpFunctions = [setVariableFromValue, setVariableFromCombined, setVariableFromModifiedValue];
		limitParams =  [false, function(values){ return (parseInt(values[1]) - parseInt(values[0]) + 1); }];
		leaseParams = [false, function(value){ return value + "h"; }];
		dhcpParams = [false, limitParams,leaseParams];

		setVariables(dhcpIds, dhcpVisIds, uci, dhcpPkgs, dhcpSections, dhcpOptions, dhcpFunctions, dhcpParams);

		dhcpWillBeEnabled = true;
		if(document.getElementById("dhcp_enabled").checked )
		{
			uci.remove("dhcp", "lan", "ignore");
			uci.set("dhcp","lan","dhcpv6",document.getElementById("dhcpv6").value);
			uci.set("dhcp","lan","ra",document.getElementById("ra").value);
			uci.set("dhcp","lan","ra_slaac",document.getElementById("ra_slaac").value);
		}
		else
		{
			uci.set("dhcp", "lan", "ignore", "1");
			uci.set("dhcp","lan","dhcpv6","disabled");
			uci.set("dhcp","lan","ra","disabled");
			uci.set("dhcp","lan","ra_slaac","0");
			dhcpWillBeEnabled = false;
		}

		// Unified Devices table: one "config host" section per device.
		// A device may carry any of: fixed IP, IPv6 suffix/DUID, group.
		// MAC is required. Exactly one section per device => no duplicate
		// dhcp-host lines => dnsmasq cannot be crashed by this page.
		var devTable = document.getElementById('devices_table_container').firstChild;
		var devData = getTableDataArray(devTable, true, false);
		for(var devIdx = 0; devIdx < devData.length; devIdx++)
		{
			var row = devData[devIdx];
			var rMac = row[1];
			if(rMac == "" || rMac == "-") { continue; }   // MAC required
			var cfgid = "device_" + (devIdx + 1);
			uci.set("dhcp", cfgid, "", "host");

			var rName = row[0];
			if(rName != "" && rName != "-") { uci.set("dhcp", cfgid, "name", rName); }

			uci.set("dhcp", cfgid, "mac", rMac);

			var rIp = row[2];
			if(rIp != "" && rIp != "-") { uci.set("dhcp", cfgid, "ip", rIp); }

			var rHostid = row[3];
			if(rHostid != "" && rHostid != "-")
			{
				var splitHostId = rHostid.split(':');
				if(splitHostId.length == 4)
				{
					splitHostId[3] = ('0000' + splitHostId[3]).slice(-4);
				}
				uci.set("dhcp", cfgid, "hostid", splitHostId.join(''));
			}

			var rDuid = row[4];
			if(rDuid != "" && rDuid != "-") { uci.set("dhcp", cfgid, "duid", rDuid); }

			var rGroup = row[5];
			if(rGroup != "" && rGroup != "-") { uci.set("dhcp", cfgid, "group", rGroup); }

			staticHostCommands.push("uci set dhcp." + cfgid + "=host");
		}

		// We don't use /etc/ethers anymore
		createEtherCommands = [ "touch /etc/ethers", "rm /etc/ethers" ];
		var dnsmasqsec = uci.getAllSectionsOfType("dhcp","dnsmasq");
		if(dnsmasqsec.length > 0)
		{
			uci.set("dhcp",dnsmasqsec[0],"readethers","0");
		}

		createHostCommands = [ "touch /etc/hosts", "rm /etc/hosts" ];
		createHostCommands.push("echo \"127.0.0.1\tlocalhost localhost4\" >> /etc/hosts");
		createHostCommands.push("echo \"::1\tlocalhost localhost6\" >> /etc/hosts");

		var firewallCommands = [];
		var firewallDefaultSections = uci.getAllSectionsOfType("firewall", "defaults");
		var oldBlockMismatches = uciOriginal.get("firewall", firewallDefaultSections[0], "enforce_dhcp_assignments") == "1" ? true : false;
		var newBlockMismatches = document.getElementById("block_mismatches").checked;
		if(newBlockMismatches != oldBlockMismatches)
		{
			if(newBlockMismatches)
			{
				uci.set("firewall", firewallDefaultSections[0], "enforce_dhcp_assignments", "1");
				firewallCommands.push("uci set firewall.@defaults[0].enforce_dhcp_assignments=1");
			}
			else
			{
				uci.remove("firewall", firewallDefaultSections[0], "enforce_dhcp_assignments");
				firewallCommands.push("uci del firewall.@defaults[0].enforce_dhcp_assignments");
			}
			firewallCommands.push("uci commit");
		}

		//need to restart firewall here because for add/remove of static ips, we need to restart bandwidth monitor, as well as for firewall commands above if we have any
		var restartDhcpCommand = "\n/etc/init.d/dnsmasq restart ; \n/etc/init.d/odhcpd restart ; \nsh /usr/lib/gargoyle/restart_firewall.sh ; \n/usr/lib/gargoyle/manage_groups.sh\n" ;

		commands = staticHostCommands.join("\n") + "\n" + uci.getScriptCommands(uciOriginal) + "\n" + createEtherCommands.join("\n") + "\n" + createHostCommands.join("\n") + "\n" + firewallCommands.join("\n") + "\n" + restartDhcpCommand ;

		var param = getParameterDefinition("commands", commands) + "&" + getParameterDefinition("hash", document.cookie.replace(/^.*hash=/,"").replace(/[\t ;]+.*$/, ""));

		var stateChangeFunction = function(req)
		{
			if(req.readyState == 4)
			{
				uciOriginal = uci.clone();
				dhcpEnabled = dhcpWillBeEnabled;
				dhcpWillBeEnabled = null;
				resetData();
				setControlsEnabled(true);
				//alert(req.responseText);
			}
		}
		runAjax("POST", "utility/run_commands.sh", param, stateChangeFunction);
	}
}

function createEditButton()
{
	var editButton = createInput("button");
	editButton.textContent = UI.Edit;
	editButton.className = "btn btn-default btn-edit";
	editButton.onclick = editDeviceModal;
	return editButton;
}

function resetData()
{
	dhcpEnabled = uciOriginal.get("dhcp", "lan", "ignore") == "1" ? false : true;

	// Build the single unified Devices table from every "config host" section,
	// regardless of section name (migrates old static_host_*/known_device_*).
	var devTableData = [];
	hostSections = uciOriginal.getAllSectionsOfType("dhcp","host");
	var secIndex=0;
	for(secIndex=0; secIndex < hostSections.length ; secIndex++)
	{
		var hostSection = hostSections[secIndex];
		var host = uciOriginal.get("dhcp",hostSection,"name");
		var mac = uciOriginal.get("dhcp",hostSection,"mac");
		if(mac == "") { continue; }
		var ipv4 = uciOriginal.get("dhcp",hostSection,"ip");
		var hostid = uciOriginal.get("dhcp",hostSection,"hostid");
		var ipv6 = "-";
		if(hostid != "")
		{
			var disp = ("00000000" + hostid).slice(-8).replace(/([0-9a-f]{4})([0-9a-f]{4})/i,"::$1:$2");
			ipv6 = validateIP6(disp) == 0 ? ip6_canonical(disp) : "-";
		}
		var duid = uciOriginal.get("dhcp",hostSection,"duid");
		duid = duid == "" ? "-" : duid;
		var group = uciOriginal.get("dhcp",hostSection,"group");
		group = group == "" ? "-" : group;

		//Name, MAC, IPv4, IPv6 suffix, DUID, Group, Edit btn
		devTableData.push([
			host  == "" ? "-" : host,
			mac,
			ipv4  == "" ? "-" : ipv4,
			ipv6,
			duid,
			group,
			createEditButton()
		]);
	}
	columnNames=[UI.HsNm, 'MAC', 'IPv4', dhcpS.Suff, 'DUID', dhcpS.GrpNm, ''];
	var devTable=createTable(columnNames, devTableData, "devices_table", true, false, removeDeviceCallback );
	var tableContainer = document.getElementById('devices_table_container');
	if(tableContainer.firstChild != null)
	{
		tableContainer.removeChild(tableContainer.firstChild);
	}
	tableContainer.appendChild(devTable);

	dhcpIds =  ['dhcp_start', 'dhcp_end', 'dhcp_lease'];
	dhcpPkgs = ['dhcp',['dhcp','dhcp'],'dhcp'];
	dhcpSections = [dhcpSection,[dhcpSection,dhcpSection],dhcpSection];
	dhcpOptions = ['start', ['start','limit'], 'leasetime'];

	enabledTest = function(value){return value != 1;};
	endCombineFunc= function(values) { return (parseInt(values[0])+parseInt(values[1])-1); };
	leaseModFunc = function(value)
	{
		var leaseHourValue;
		if(value.match(/.*h/))
		{
			leaseHourValue=value.substr(0,value.length-1);
		}
		else if(value.match(/.*m/))
		{
			leaseHourValue=value.substr(0,value.length-1)/(60);
		}
		else if(value.match(/.*s/))
		{
			leaseHourValue=value.substr(0,value.length-1)/(60*60);
		}
		return leaseHourValue;
	};

	dhcpParams = [100, [endCombineFunc,150],[12,leaseModFunc]];
	dhcpFunctions = [loadValueFromVariable, loadValueFromMultipleVariables, loadValueFromModifiedVariable];

	loadVariables(uciOriginal, dhcpIds, dhcpPkgs, dhcpSections, dhcpOptions, dhcpParams, dhcpFunctions);

	document.getElementById("dhcp_enabled").checked = dhcpEnabled;
	setEnabled(document.getElementById('dhcp_enabled').checked);

	var firewallDefaultSections = uciOriginal.getAllSectionsOfType("firewall", "defaults");
	var blockMismatches = uciOriginal.get("firewall", firewallDefaultSections[0], "enforce_dhcp_assignments") == "1" ? true : false;
	document.getElementById("block_mismatches").checked = blockMismatches;

	dhcpv6 = uciOriginal.get("dhcp", "lan", "dhcpv6");
	document.getElementById("dhcpv6").value = dhcpv6 == "" ? "disabled" : dhcpv6;

	ra = uciOriginal.get("dhcp", "lan", "ra");
	document.getElementById("ra").value = ra == "" ? "disabled" : ra;

	ra_slaac = uciOriginal.get("dhcp", "lan", "ra_slaac");
	document.getElementById("ra_slaac").value = ra_slaac == "" ? "0" : ra_slaac;

	var ip6txt = "";
	for(var x = 0; x < currentLanIp6.length; x++)
	{
		if(ip6_scope(currentLanIp6[x])[0] == "Global")
		{
			ip6txt = ip6txt + (x == 0 ? "" : "\n") + ip6_mask(currentLanIp6[x], currentLanMask6[x]) + "/" + currentLanMask6[x];
		}
	}
	setChildText("ip6prefix", ip6txt);

	//setup connected-hosts dropdown
	resetDeviceMacList();
}

function removeDeviceCallback(table, row)
{
	resetDeviceMacList();
}

// Populate the "select from currently connected hosts" dropdown, excluding
// MACs that already have a device row.
function resetDeviceMacList()
{
	var devTable = document.getElementById("devices_table_container").firstChild;
	var devTableData = devTable == null ? [] : getTableDataArray(devTable, true, false);
	var usedMacs = [];
	var di;
	for(di = 0; di < devTableData.length; di++)
	{
		usedMacs[ (devTableData[di][1]).toUpperCase() ] = 1;
	}

	var hmVals = [ "none" ];
	var hmText = [ dhcpS.SelH ];
	var leaseIndex = 0;
	for(leaseIndex=0; leaseIndex < leaseData.length; leaseIndex++)
	{
		var lease = leaseData[leaseIndex];
		var mac = (lease[0]).toUpperCase();
		if( usedMacs[ mac ] == null )
		{
			// value = hostname,mac,currentIp  (currentIp pre-fills the Fixed IP field)
			hmVals.push( lease[2] + "," + mac + "," + lease[1] );
			hmText.push( (lease[2] == "" || lease[2] == "*" ? lease[1] : lease[2] ) + " (" + mac + ")" );
		}
	}
	setAllowableSelections("dev_from_connected", hmVals, hmText);

	var hmEnabled = hmText.length > 1 && document.getElementById('dhcp_enabled').checked ? true : false;
	setElementEnabled(document.getElementById("dev_from_connected"), hmEnabled, "none");
}

function setEnabled(enabled)
{
	var ids=['dhcp_start', 'dhcp_end', 'dhcp_lease', 'block_mismatches', 'dhcpv6', 'ra', 'add_device_button'];
	var idIndex;
	for (idIndex in ids)
	{
		var element = document.getElementById(ids[idIndex]);
		setElementEnabled(element, enabled, "");
	}

	var devTable = document.getElementById('devices_table_container').firstChild;
	setRowClasses(devTable, enabled);

	resetDeviceMacList();
}

function validateDHCPHostName(hostname)
{
	if(hostname == '')
	{
		// Hostname is optional
		return 0;
	}
	else if(hostname.match(/^[a-zA-Z0-9-]+$/) == null)
	{
		// No special symbols in hostnames
		return 1;
	}
	return 0;
}

function proofreadDHCPHostName(input)
{
	proofreadText(input, validateDHCPHostName, 0);
}

function proofreadAll()
{
	dhcpIds = ['dhcp_start', 'dhcp_end', 'dhcp_lease'];
	labelIds= ['dhcp_start_label', 'dhcp_end_label', 'dhcp_lease_label'];
	functions = [validateNumeric, validateNumeric, validateNumeric];
	returnCodes = [0,0,0];
	visibilityIds= dhcpIds;
	errors = proofreadFields(dhcpIds, labelIds, functions, returnCodes, visibilityIds);

	//test that dhcp range is within subnet
	if(errors.length == 0 && document.getElementById("dhcp_enabled").checked)
	{
		var dhcpSection = getDhcpSection(uciOriginal);
		var mask = uciOriginal.get("network", "lan", "netmask");
		var ip = uciOriginal.get("network", "lan", "ipaddr");
		var start = parseInt(document.getElementById("dhcp_start").value);
		var end = parseInt(document.getElementById("dhcp_end").value );
		if(!rangeInSubnet(mask, ip, start, end))
		{
			errors.push(dhcpS.dsubErr);
		}

		var ipEnd = parseInt( (ip.split("."))[3] );
		if(ipEnd >= start && ipEnd <= end)
		{
			errors.push(dhcpS.dipErr);
		}
	}

	return errors;
}

// ---- Unified Devices ----

function setDeviceAdvancedVisible(visible)
{
	var c = document.getElementById("dev_advanced_container");
	var t = document.getElementById("dev_advanced_toggle");
	if(c) { c.style.display = visible ? "" : "none"; }
	if(t) { t.textContent = visible ? (dhcpS.HideAdv || "Hide advanced (IPv6 reservation)") : (dhcpS.ShowAdv || "Show advanced (IPv6 reservation)"); }
}

function toggleDeviceAdvanced()
{
	var c = document.getElementById("dev_advanced_container");
	if(!c) { return; }
	setDeviceAdvancedVisible(c.style.display == "none");
}

function populateGroupDatalist()
{
	var datalist = document.getElementById("dev_group_list");
	if(!datalist) { return; }
	while(datalist.firstChild) { datalist.removeChild(datalist.firstChild); }

	var seen = {};
	var gi;
	for(gi = 0; gi < knownDeviceGroups.length; gi++)
	{
		seen[knownDeviceGroups[gi]] = 1;
	}
	var devTable = document.getElementById("devices_table_container").firstChild;
	if(devTable)
	{
		var devData = getTableDataArray(devTable, true, false);
		var ri;
		for(ri = 0; ri < devData.length; ri++)
		{
			var grp = devData[ri][5];
			if(grp && grp != "-") { seen[grp] = 1; }
		}
	}
	var grpName;
	for(grpName in seen)
	{
		var opt = document.createElement("option");
		opt.value = grpName;
		datalist.appendChild(opt);
	}
}

function proofreadDevice(excludeRow)
{
	var validateOptionalIp = function(val)
	{
		if(val == "" || val == "-") { return 0; }
		return validateIP(val);
	};
	var proofreadIP6Suffix = function(val)
	{
		if(val.length == 0 || val == "-") { return 0; }   // optional
		if(!val.match(/^::([0-9a-f]{0,4}:)?[0-9a-f]{0,4}/)) { return 1; }
		return 0;
	};
	var proofreadDUID = function(val)
	{
		if(val == "" || val == "-") { return 0; }          // optional
		if(!val.match(/^[0-9a-f]{0,130}$/i)) { return 1; }
		return 0;
	};

	var addIds    = ['dev_name', 'dev_mac', 'dev_ip', 'dev_hostid', 'dev_duid'];
	var labelIds  = ['dev_name_label', 'dev_mac_label', 'dev_ip_label', 'dev_hostid_label', 'dev_duid_label'];
	var functions = [validateDHCPHostName, validateMac, validateOptionalIp, proofreadIP6Suffix, proofreadDUID];
	var returnCodes = [0,0,0,0,0];
	var errors = proofreadFields(addIds, labelIds, functions, returnCodes, addIds, document);

	// Group is optional, but if set it must be a safe name: letters, digits,
	// hyphen, underscore only. Spaces / special chars break the GROUP:<name>
	// references used by firewall/quota/restriction rules and the nftables set.
	var devGroupEl = document.getElementById("dev_group");
	var validateGroupName = function(val)
	{
		if(val == "" || val == "-") { return 0; }            // optional
		return val.match(/^[a-zA-Z0-9_-]+$/) == null ? 1 : 0;
	};
	if(devGroupEl)
	{
		proofreadText(devGroupEl, validateGroupName, 0);
		if(validateGroupName(devGroupEl.value) != 0)
		{
			errors.push(dhcpS.grpErr);
		}
	}

	var nameVal = document.getElementById('dev_name').value;
	var macVal  = document.getElementById('dev_mac').value;
	var ipVal   = document.getElementById('dev_ip').value;
	var hidVal  = document.getElementById('dev_hostid').value;
	var duidVal = document.getElementById('dev_duid').value;

	if(errors.length == 0)
	{
		var devTable = document.getElementById('devices_table_container').firstChild;
		var currentData = getTableDataArray(devTable, true, false);
		var rowDataIndex = 0;
		for (rowDataIndex=0; rowDataIndex < currentData.length ; rowDataIndex++)
		{
			if(devTable.rows[rowDataIndex+1] == excludeRow) { continue; }
			var rowData = currentData[rowDataIndex];
			if(nameVal != '' && nameVal != '-' && rowData[0] == nameVal)
			{
				errors.push(dhcpS.dHErr);
			}
			if(rowData[1].toUpperCase() == macVal.toUpperCase())
			{
				errors.push(dhcpS.dMErr);
			}
			if(ipVal != '' && ipVal != '-' && rowData[2] == ipVal)
			{
				errors.push(dhcpS.dIPErr);
			}
			if(hidVal != '' && hidVal != '-' && rowData[3] != '-' && rowData[3] == hidVal)
			{
				errors.push(dhcpS.dHIDErr);
			}
			if(duidVal != '' && duidVal != '-' && rowData[4] != '-' && rowData[4] == duidVal)
			{
				errors.push(dhcpS.dDUIDErr);
			}
		}
	}

	// IPv6 suffix requires a DUID
	if(errors.length == 0)
	{
		if((hidVal != "" && hidVal != "-") && (duidVal == "" || duidVal == "-"))
		{
			errors.push(dhcpS.NoDUID);
		}
	}

	// Fixed IP, when set, must be inside the LAN subnet and not the router IP
	if(errors.length == 0 && ipVal != "" && ipVal != "-")
	{
		var mask = uciOriginal.get("network", "lan", "netmask");
		var ip = uciOriginal.get("network", "lan", "ipaddr");
		var testEnd = parseInt( (ipVal.split("."))[3] );
		if(!rangeInSubnet(mask, ip, testEnd, testEnd))
		{
			errors.push(dhcpS.subErr);
		}
		if(ip == ipVal)
		{
			errors.push(dhcpS.ipErr);
		}
	}

	return errors;
}

function addDevice()
{
	var errors = proofreadDevice(null);
	if(errors.length > 0)
	{
		alert(errors.join("\n") + "\n\n" + dhcpS.AErr);
	}
	else
	{
		var dName  = document.getElementById('dev_name').value;
		var dMac   = document.getElementById('dev_mac').value;
		var dIp    = document.getElementById('dev_ip').value;
		var dHid   = document.getElementById('dev_hostid').value;
		var dDuid  = document.getElementById('dev_duid').value;
		var dGroup = document.getElementById('dev_group').value;

		var values = [
			dName  == "" ? "-" : dName,
			dMac,
			dIp    == "" ? "-" : dIp,
			dHid   == "" ? "-" : dHid,
			dDuid  == "" ? "-" : dDuid,
			dGroup == "" ? "-" : dGroup,
			createEditButton()
		];
		var devTable = document.getElementById('devices_table_container').firstChild;
		addTableRow(devTable, values, true, false, removeDeviceCallback);
		resetDeviceMacList();
		closeModalWindow('device_modal');
	}
}

function editDevice(editRow)
{
	var errors = proofreadDevice(editRow);
	if(errors.length > 0)
	{
		alert(errors.join("\n") + "\n" + dhcpS.upErr);
	}
	else
	{
		var dName  = document.getElementById('dev_name').value;
		var dMac   = document.getElementById('dev_mac').value;
		var dIp    = document.getElementById('dev_ip').value;
		var dHid   = document.getElementById('dev_hostid').value;
		var dDuid  = document.getElementById('dev_duid').value;
		var dGroup = document.getElementById('dev_group').value;

		editRow.childNodes[0].firstChild.data = dName  == "" ? "-" : dName;
		editRow.childNodes[1].firstChild.data = dMac;
		editRow.childNodes[2].firstChild.data = dIp    == "" ? "-" : dIp;
		editRow.childNodes[3].firstChild.data = dHid   == "" ? "-" : dHid;
		editRow.childNodes[4].firstChild.data = dDuid  == "" ? "-" : dDuid;
		editRow.childNodes[5].firstChild.data = dGroup == "" ? "-" : dGroup;

		closeModalWindow('device_modal');
		resetDeviceMacList();
	}
}

function addDeviceModal()
{
	populateGroupDatalist();
	var modalButtons = [
		{"title" : UI.Add, "classes" : "btn btn-primary", "function" : addDevice},
		"defaultDismiss"
	];

	var name = "";
	var mac  = "";
	var ip   = "";
	var selectedVal = getSelectedValue("dev_from_connected");
	if(selectedVal != "none")
	{
		var parts = selectedVal.split(/,/);
		name = parts[0];
		mac  = parts[1];
		ip   = (parts[2] == null || parts[2] == "*") ? "" : parts[2];
		setSelectedValue("dev_from_connected", "none");
	}

	var modalElements = [
		{"id" : "dev_name",   "value" : name == "*" ? "" : name},
		{"id" : "dev_mac",    "value" : mac},
		{"id" : "dev_ip",     "value" : ip},
		{"id" : "dev_hostid", "value" : ""},
		{"id" : "dev_duid",   "value" : ""},
		{"id" : "dev_group",  "value" : ""}
	];
	modalPrepare('device_modal', dhcpS.AdDev, modalElements, modalButtons);
	setDeviceAdvancedVisible(false);
	openModalWindow('device_modal');
}

function editDeviceModal()
{
	var editRow = this.parentNode.parentNode;
	populateGroupDatalist();
	var modalButtons = [
		{"title" : UI.CApplyChanges, "classes" : "btn btn-primary", "function" : function() { editDevice(editRow); }},
		"defaultDiscard"
	];

	var dName  = editRow.childNodes[0].firstChild.data;
	var dMac   = editRow.childNodes[1].firstChild.data;
	var dIp    = editRow.childNodes[2].firstChild.data;
	var dHid   = editRow.childNodes[3].firstChild.data;
	var dDuid  = editRow.childNodes[4].firstChild.data;
	var dGroup = editRow.childNodes[5].firstChild.data;

	var modalElements = [
		{"id" : "dev_name",   "value" : dName  == "-" ? "" : dName},
		{"id" : "dev_mac",    "value" : dMac},
		{"id" : "dev_ip",     "value" : dIp    == "-" ? "" : dIp},
		{"id" : "dev_hostid", "value" : dHid   == "-" ? "" : dHid},
		{"id" : "dev_duid",   "value" : dDuid  == "-" ? "" : dDuid},
		{"id" : "dev_group",  "value" : dGroup == "-" ? "" : dGroup}
	];
	modalPrepare('device_modal', dhcpS.EDev, modalElements, modalButtons);
	// auto-expand the advanced section only if this device already uses IPv6 fields
	var hasAdvanced = (dHid != "-" && dHid != "") || (dDuid != "-" && dDuid != "");
	setDeviceAdvancedVisible(hasAdvanced);
	openModalWindow('device_modal');
}
