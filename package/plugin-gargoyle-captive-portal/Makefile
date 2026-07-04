include $(TOPDIR)/rules.mk

PKG_NAME:=plugin-gargoyle-captive-portal
PKG_VERSION:=$(GARGOYLE_VERSION)
ifeq ($(GARGOYLE_VERSION),)
     PKG_VERSION:=1.0.0
endif
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)

include $(INCLUDE_DIR)/package.mk

define Package/plugin-gargoyle-captive-portal
	SECTION:=admin
	CATEGORY:=Administration
	SUBMENU:=Gargoyle Web Interface
	TITLE:=Captive Portal for Guest WiFi
	DEPENDS:=+gargoyle +gargoyle-firewall-util +uhttpd +iw
	MAINTAINER:=Gargoyle
endef

define Package/plugin-gargoyle-captive-portal/description
	Guest-network captive portal (click-through or password auth). Gates WAN
	access for devices connected to a Gargoyle guest wifi network without
	introducing a separate guest subnet/zone - built entirely on top of the
	existing bridge-based guest wifi isolation.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Configure
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/captive_portal_passwd \
		$(PKG_BUILD_DIR)/captive_portal_passwd.c
endef

define Package/plugin-gargoyle-captive-portal/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	# Run the setup logic directly rather than invoking
	# /etc/uci-defaults/captive_portal.ucidefaults - that file is also
	# consumed automatically by the system's own generic uci-defaults
	# runner, and racing against it (confirmed empirically: the file is
	# already gone by the time this postinst tries to use it, and the
	# automatic runner does not reliably complete every uci-defaults
	# script all the way through) silently leaves the second uhttpd
	# instance unconfigured. The file stays in the package for the
	# genuine first-boot/image-bake case, which doesn't hit this race.
	uci set firewall.captiveportal_include_file=include
	uci set firewall.captiveportal_include_file.path='/etc/captive_portal.firewall'
	[ -z "$$(command -v fw4)" ] && uci set firewall.captiveportal_include_file.reload='1'

	port="$$(uci -q get captive_portal.global.port)"
	[ -z "$$port" ] && port='8082'
	uci set uhttpd.captive_portal=uhttpd
	uci -q delete uhttpd.captive_portal.listen_http
	uci add_list uhttpd.captive_portal.listen_http="0.0.0.0:$${port}"
	uci set uhttpd.captive_portal.home='/www-captive-portal'
	uci set uhttpd.captive_portal.index_page='index.sh'
	uci set uhttpd.captive_portal.error_page='/index.sh'
	uci set uhttpd.captive_portal.no_dirlists='1'
	uci -q delete uhttpd.captive_portal.interpreter
	uci add_list uhttpd.captive_portal.interpreter='.sh=/usr/bin/run_haserl'

	menu_name="Captive Portal"
	if [ -n "$$(command -v i18n-menu)" ]; then
		mn="$$(i18n-menu gargoyle.display.firewall_captive_portal)"
		[ -n "$$mn" ] && menu_name="$$mn"
	fi
	uci set gargoyle.display.firewall_captive_portal="$$menu_name"
	uci set gargoyle.scripts.firewall_captive_portal='captive_portal.sh'
	uci set gargoyle.firewall.captive_portal='130'

	uci commit uhttpd
	uci commit firewall
	uci commit gargoyle

	/etc/init.d/uhttpd restart 2>/dev/null
fi
endef

define Package/plugin-gargoyle-captive-portal/prerm
#!/bin/sh
sh /etc/captive_portal.firewall stop 2>/dev/null
endef

define Package/plugin-gargoyle-captive-portal/postrm
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	uci -q delete firewall.captiveportal_include_file
	uci -q delete uhttpd.captive_portal
	uci -q delete gargoyle.display.firewall_captive_portal
	uci -q delete gargoyle.scripts.firewall_captive_portal
	uci -q delete gargoyle.firewall.captive_portal
	uci commit
fi
grep -v update_captive_portal_nftset /etc/crontabs/root > /tmp/cp.cron.bak 2>/dev/null
mv /tmp/cp.cron.bak /etc/crontabs/root 2>/dev/null
/etc/init.d/cron restart 2>/dev/null
endef

define Package/plugin-gargoyle-captive-portal/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/captive_portal_passwd $(1)/usr/sbin/
	$(INSTALL_BIN) ./files/usr/sbin/update_captive_portal_nftset $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/usr/lib/captive_portal
	$(INSTALL_BIN) ./files/usr/lib/captive_portal/captive_portal_common.sh $(1)/usr/lib/captive_portal/

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/captive_portal $(1)/etc/config/

	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/etc/uci-defaults/captive_portal.ucidefaults $(1)/etc/uci-defaults/

	$(INSTALL_BIN) ./files/etc/captive_portal.firewall $(1)/etc/

	$(INSTALL_DIR) $(1)/www
	$(INSTALL_BIN) ./files/www/captive_portal.sh $(1)/www/
	$(INSTALL_DIR) $(1)/www/js
	$(INSTALL_DATA) ./files/www/js/captive_portal.js $(1)/www/js/
	$(INSTALL_DIR) $(1)/www/i18n/universal
	$(INSTALL_DATA) ./files/www/i18n/universal/menu-firewall_captive_portal.txt $(1)/www/i18n/universal/
	$(INSTALL_DIR) $(1)/www/i18n/English-EN
	$(INSTALL_DATA) ./files/www/i18n/English-EN/captive_portal.js $(1)/www/i18n/English-EN/

	$(INSTALL_DIR) $(1)/www-captive-portal
	$(INSTALL_BIN) ./files/www-captive-portal/index.sh $(1)/www-captive-portal/
	$(INSTALL_BIN) ./files/www-captive-portal/auth.sh $(1)/www-captive-portal/
endef

$(eval $(call BuildPackage,plugin-gargoyle-captive-portal))
