include $(TOPDIR)/rules.mk

PKG_NAME:=plugin-gargoyle-captive-portal
PKG_VERSION:=20240101
PKG_RELEASE:=1.0.0

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/plugin-gargoyle-captive-portal
	SECTION:=admin
	CATEGORY:=Administration
	SUBMENU:=Gargoyle Web Interface
	TITLE:=Captive Portal support for Gargoyle
	MAINTAINER:=Gargoyle Contributors
	DEPENDS:=+gargoyle +uhttpd
	PKGARCH:=all
endef

define Package/plugin-gargoyle-captive-portal/description
	Captive portal (splash page / guest authentication) for Gargoyle.
	Intercepts HTTP traffic on the guest wireless network and redirects
	new clients to a configurable landing page before granting internet
	access.  Authenticated clients are tracked via an nftables set with
	automatic session timeout.
endef

define Build/Prepare
endef

define Build/Configure
endef

define Build/Compile
endef

define Package/plugin-gargoyle-captive-portal/postinst
#!/bin/sh

if [ -z "$${IPKG_INSTROOT}" ]; then
	menu_name="Captive Portal"
	[ -n "$(which i18n)" ] && {
		mn=$$(i18n-menu gargoyle.display.firewall_captive_portal)
		if [ -n "$$mn" ] ; then
			menu_name="$$mn"
		fi
	}
	uci set gargoyle.display.firewall_captive_portal="$$menu_name"
	uci set gargoyle.scripts.firewall_captive_portal='captive_portal.sh'
	uci set gargoyle.firewall.captive_portal='130'
	uci commit gargoyle
fi
endef

define Package/plugin-gargoyle-captive-portal/prerm
#!/bin/sh

sh /usr/lib/gargoyle/captive_portal.sh teardown
endef

define Package/plugin-gargoyle-captive-portal/postrm
#!/bin/sh

if [ -z "$${IPKG_INSTROOT}" ]; then
	uci del gargoyle.display.firewall_captive_portal
	uci del gargoyle.scripts.firewall_captive_portal
	uci del gargoyle.firewall.captive_portal
	uci commit gargoyle
fi
endef

define Package/plugin-gargoyle-captive-portal/install
	$(INSTALL_DIR) $(1)
	$(CP) ./files/* $(1)/
endef

$(eval $(call BuildPackage,plugin-gargoyle-captive-portal))
