#
# Copyright (C) 2006 OpenWrt.org
# Copyright (C) 2009 Eric Bishop <eric@gargoyle-router.com>
# Copyright (C) 2017 Michael Gray <michael.gray@lantisproject.com>
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=libipttmctl
PKG_VERSION:=$(GARGOYLE_VERSION)
ifeq ($(GARGOYLE_VERSION),)
     PKG_VERSION:=1.0.0
endif
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/libipttmctl

include $(INCLUDE_DIR)/package.mk

define Package/libipttmctl
	SECTION:=net
	CATEGORY:=Network
	DEPENDS:=+iptables-mod-timemon +libiptbwctl
	TITLE:=IPT timemon control library
	URL:=http://www.gargoyle-router.com
	MAINTAINER:=Michael Gray <michael.gray@lantisproject.com>
endef


define Package/libipttmctl/description
  IPT bandwidth control library
endef


define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef


define Build/Configure
endef

define Build/Compile
	-$(MAKE) -C $(PKG_BUILD_DIR) clean
	$(MAKE) -C $(PKG_BUILD_DIR) \
		$(TARGET_CONFIGURE_OPTS) \
		STAGING_DIR="$(STAGING_DIR)" \
		CFLAGS="$(TARGET_CFLAGS) -I $(STAGING_DIR)/usr/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -L $(STAGING_DIR)/usr/lib" \
		all
	
	mkdir -p $(STAGING_DIR)/usr/include/
	$(CP) $(PKG_BUILD_DIR)/*.h $(STAGING_DIR)/usr/include/

	mkdir -p $(STAGING_DIR)/usr/lib
	$(CP) $(PKG_BUILD_DIR)/*.so* $(STAGING_DIR)/usr/lib/
	
	$(MAKE) -C $(PKG_BUILD_DIR)/utils \
		$(TARGET_CONFIGURE_OPTS) \
		STAGING_DIR="$(STAGING_DIR)" \
		CFLAGS="$(TARGET_CFLAGS) -I $(STAGING_DIR)/usr/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -L $(STAGING_DIR)/usr/lib" \
		all
endef



define Package/libipttmctl/install
	$(INSTALL_DIR) $(1)/usr/lib
	$(INSTALL_DIR) $(1)/usr/bin
	$(CP) $(PKG_BUILD_DIR)/*.so* $(1)/usr/lib/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/utils/tm_get $(1)/usr/bin/tm_get
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/utils/tm_set $(1)/usr/bin/tm_set
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/utils/tm_print_history_file $(1)/usr/bin/tm_print_history_file
endef


$(eval $(call BuildPackage,libipttmctl))
