/*
 * This program is copyright © 2008-2013 Eric Bishop and is distributed under the terms of the GNU GPL
 * version 2.0 with a special clarification/exception that permits adapting the program to
 * configure proprietary "back end" software provided that all modifications to the web interface
 * itself remain covered by the GPL.
 * See http://gargoyle-router.com/faq.html#qfoss for more information
 */

var vlanStr = new Object(); // part of i18n

function resetData()
{
	var hwLabel = document.getElementById("hw_model_label");
	if(vlanHwModel === "dsa")
	{
		hwLabel.textContent = vlanStr.HWDsa;
	}
	else if(vlanHwModel === "swconfig")
	{
		hwLabel.textContent = vlanStr.HWSwconfig;
	}
	else
	{
		hwLabel.textContent = vlanStr.HWNone;
	}

	var wanLabel = document.getElementById("wan_vlan_label");
	if(wanVlanActive)
	{
		wanLabel.textContent = "VLAN " + wanVlanId + " (" + wanVlanBase + ")";
	}
	else
	{
		wanLabel.textContent = vlanStr.WanNoVlan;
	}

	document.getElementById("bridge_vlan_filter_label").textContent =
		bridgeVlanFiltering === "1" ? vlanStr.Enabled : vlanStr.Disabled;

	if(vlanInterfaces.length > 0)
	{
		var panel = document.getElementById("vlan_iface_panel");
		panel.style.display = "";

		var tableData = [];
		for(var i = 0; i < vlanInterfaces.length; i++)
		{
			tableData.push([vlanInterfaces[i].name, vlanInterfaces[i].device]);
		}
		var container = document.getElementById("vlan_iface_table_container");
		container.innerHTML = "";
		var table = createTable([vlanStr.IfaceName, vlanStr.IfaceDev], tableData, "vlan_iface_table", false, false, false);
		container.appendChild(table);
	}
}
