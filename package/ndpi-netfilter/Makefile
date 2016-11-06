include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=ndpi-netfilter
PKG_VERSION:=3.0
PKG_RELEASE:=1
PKG_REV:=052718d2cf39491d56b3e65d553bad2b8d7ae132

PKG_SOURCE_PROTO:=git
PKG_SOURCE:=$(PKG_NAME)-$(PKG_VERSION).tar.bz2
PKG_SOURCE_SUBDIR:=$(PKG_NAME)-$(PKG_VERSION)
PKG_SOURCE_URL:=https://github.com/betolj/ndpi-netfilter
PKG_SOURCE_VERSION:=$(PKG_REV)

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)

include $(INCLUDE_DIR)/package.mk

define Package/iptables-mod-ndpi
  SUBMENU:=Firewall
  SECTION:=net
  CATEGORY:=Network
  TITLE:=nDPI successor of OpenDPI
  URL:=http://www.ntop.org/products/ndpi/
  DEPENDS:= +iptables +iptables-mod-conntrack-extra +kmod-ipt-ndpi
  MAINTAINER:=Thomas Heil <heil@terminal-consulting.de>
endef

define Package/iptables-mod-ndpi/description
  nDPI is a ntop-maintained superset of the popular OpenDPI library
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	tar -zxvf $(PKG_BUILD_DIR)/nDPI.tar.gz -C $(PKG_BUILD_DIR)
endef

MAKE_FLAGS +=\
	KERNEL_DIR="$(LINUX_DIR)" \
	ARCH="$(LINUX_KARCH)" \
	NDPI_PATH=$(PKG_BUILD_DIR)/nDPI \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	all

define Package/iptables-mod-ndpi/install
	$(INSTALL_DIR) $(1)/usr/lib/iptables
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/ipt/libxt_ndpi.so $(1)/usr/lib/iptables
endef

define KernelPackage/ipt-ndpi
  SUBMENU:=Netfilter Extensions
  TITLE:=nDPI netfilter module
  DEPENDS:= +kmod-nf-conntrack +kmod-nf-conntrack-netlink +kmod-ipt-compat-xtables
  FILES:= \
	$(PKG_BUILD_DIR)/src/xt_ndpi.ko
  AUTOLOAD:$(call AutoLoad,45,xt_ndpi)
endef

$(eval $(call BuildPackage,iptables-mod-ndpi))
$(eval $(call KernelPackage,ipt-ndpi))
