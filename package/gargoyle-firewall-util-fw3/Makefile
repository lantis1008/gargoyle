include $(TOPDIR)/rules.mk

PKG_NAME:=gargoyle-firewall-util-fw3
PKG_VERSION:=$(GARGOYLE_VERSION)
ifeq ($(GARGOYLE_VERSION),)
     PKG_VERSION:=1.0.0
endif
PKG_RELEASE:=1

PKG_BUILD_DIR := $(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/gargoyle-firewall-util-fw3
	SECTION:=net
	CATEGORY:=Network
	TITLE:=A couple of shell script routines for firewall (fw3) initialization
	DEPENDS:=+iptables-zz-legacy +ip6tables-zz-legacy +ebtables-legacy +kmod-ebtables-ipv4 +kmod-ebtables-ipv6 +libericstools +uci +libiptbwctl +iptables-mod-filter +iptables-mod-ipopt +iptables-mod-conntrack-extra +iptables-mod-nat-extra +iptables-mod-extra +iptables-mod-iprange +iptables-mod-bandwidth +iptables-mod-timerange +iptables-mod-weburl +kmod-tun +kmod-nf-nathelper +kmod-nf-nathelper-extra +ipset +kmod-ipt-ipset
	MAINTAINER:=Eric Bishop <eric@gargoyle-router.com>
endef

define Package/gargoyle-firewall-util-fw3/description
	A couple of shell script routines for firewall initialization
endef

define Build/Prepare
	echo PACKAGE BUILD DIR = $(PACKAGE_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef


define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		$(TARGET_CONFIGURE_OPTS) \
		STAGING_DIR="$(STAGING_DIR)" \
		CFLAGS="$(TARGET_CFLAGS) -I $(STAGING_DIR)/usr/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -L $(STAGING_DIR)/usr/lib" 
endef



define Package/gargoyle-firewall-util-fw3/postinst
	included=$$(cat $${IPKG_INSTROOT}/etc/config/firewall | grep 'gargoyle_additions.firewall'  )
	if [ -z "$$included" ] ; then printf "config include\n\toption type script\n\toption path /usr/lib/gargoyle_firewall_util/gargoyle_additions.firewall\n\toption family any\n\toption reload 1\n\n" >> $${IPKG_INSTROOT}/etc/config/firewall ; fi
endef

define Package/gargoyle-firewall-util-fw3/install
	$(INSTALL_DIR) $(1)/usr/lib/gargoyle_firewall_util/
	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface/
	$(INSTALL_DIR) $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/init.d/
	$(INSTALL_DIR) $(1)/etc/ppp/ip-up.d/
	
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/make_iptables_rules $(1)/usr/bin/make_iptables_rules
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/delete_chain_from_table $(1)/usr/bin/delete_chain_from_table
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/backup_quotas $(1)/usr/bin/backup_quotas
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/restore_quotas $(1)/usr/bin/restore_quotas
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/print_quotas $(1)/usr/bin/print_quotas
	
	$(INSTALL_BIN) ./files/cache_dhcpv4_leases.sh $(1)/usr/lib/gargoyle_firewall_util/cache_dhcpv4_leases.sh
	$(INSTALL_BIN) ./files/gargoyle_firewall_util.sh $(1)/usr/lib/gargoyle_firewall_util/gargoyle_firewall_util.sh
	$(INSTALL_BIN) ./files/gargoyle_additions.firewall $(1)/usr/lib/gargoyle_firewall_util/gargoyle_additions.firewall 
	$(INSTALL_BIN) ./files/gargoyle_firewall.hotplug $(1)/etc/hotplug.d/iface/21-gargoyle_firewall
	$(INSTALL_BIN) ./files/set_kernel_timezone.init $(1)/etc/init.d/set_kernel_timezone
	$(INSTALL_BIN) ./files/modemaccess.pppoe $(1)/etc/ppp/ip-up.d/modemaccess.sh 
endef


$(eval $(call BuildPackage,gargoyle-firewall-util-fw3))
