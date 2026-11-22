# SPDX-License-Identifier: MIT
include $(TOPDIR)/rules.mk

PKG_NAME:=https-dns-proxy
PKG_VERSION:=2026.05.06
PKG_RELEASE:=1
PKG_CPE_ID:=cpe:/a:mossdef:https-dns-proxy

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/aarond10/https_dns_proxy/
PKG_MIRROR_HASH:=345f14e0a05e032754cea440f5e8798c6896cf5bc7cc12b3c7ed1c8d34e4daad
PKG_SOURCE_VERSION:=0ba0525fa7c10115cc7c92e99621bfe484ae96e7

PKG_MAINTAINER:=Michael Gray <support@lantisproject.com>
PKG_LICENSE:=MIT
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

TARGET_CFLAGS += $(FPIC)
TARGET_LDFLAGS += -Wl,--gc-sections
CMAKE_OPTIONS += -DCLANG_TIDY_EXE= -DSW_VERSION=$(PKG_VERSION)-r$(PKG_RELEASE)

define Package/https-dns-proxy-gargoyle
	SECTION:=net
	CATEGORY:=Network
	TITLE:=DNS Over HTTPS Proxy (for Gargoyle)
	URL:=https://github.com/mossdef-org/https-dns-proxy/
	DEPENDS:= \
		+libcares \
		+libcurl \
		+libev \
		+ca-bundle \
		+jsonfilter \
		+resolveip \
		+!BUSYBOX_DEFAULT_GREP:grep \
		+!BUSYBOX_DEFAULT_SED:sed
	PROVIDES:=https-dns-proxy
	CONFLICTS:=https_dns_proxy https-dns-proxy
endef

define Package/https-dns-proxy-gargoyle/description
Light-weight DNS-over-HTTPS, non-caching translation proxy for the RFC 8484 DoH standard.
It receives regular, unencrypted (UDP) DNS requests and resolves them via DoH resolver.
Based on the OpenWrt package https-dns-proxy by Stan Grishin <stangri@melmac.ca>.
Please see https://docs.mossdef.org/https-dns-proxy/ for more information.
Modified to suit the Gargoyle web interface.
endef

define Package/https-dns-proxy-gargoyle/conffiles
/etc/config/https-dns-proxy
endef

define Package/https-dns-proxy-gargoyle/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/https_dns_proxy $(1)/usr/sbin/https-dns-proxy
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/https-dns-proxy $(1)/etc/init.d/https-dns-proxy
	$(SED) "s|^\(readonly PKG_VERSION\).*|\1='$(PKG_VERSION)-r$(PKG_RELEASE)'|" $(1)/etc/init.d/https-dns-proxy
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/etc/config/https-dns-proxy $(1)/etc/config/https-dns-proxy
	$(INSTALL_DIR) $(1)/usr/share/nftables.d/ruleset-post
	$(INSTALL_DATA) ./files/usr/share/nftables.d/ruleset-post/.placeholder $(1)/usr/share/nftables.d/ruleset-post/.placeholder
endef

$(eval $(call BuildPackage,https-dns-proxy-gargoyle))
