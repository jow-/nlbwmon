include $(TOPDIR)/rules.mk

PKG_NAME:=nlbwmon
PKG_VERSION:=2017-02-08
PKG_RELEASE:=1

CMAKE_INSTALL:=1

PKG_MAINTAINER:=Jo-Philipp Wich <jo@mein.io>
PKG_LICENSE:=ISC

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

TARGET_CFLAGS += -I$(STAGING_DIR)/usr/include/libnl-tiny

define Package/nlbwmon
  SECTION:=base
  CATEGORY:=Base system
  DEPENDS:=+libubox +libnl-tiny +zlib +kmod-nf-conntrack-netlink
  TITLE:=LEDE Traffic Usage Monitor
endef

define Package/nlbwmon/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/sbin/nlbwmon $(1)/usr/sbin/nlbwmon
	$(LN) nlbwmon $(1)/usr/sbin/nlbw
	$(INSTALL_DIR) $(1)/usr/share/nlbwmon
	$(INSTALL_DATA) ./src/protocols.txt $(1)/usr/share/nlbwmon/protocols
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/nlbwmon.init $(1)/etc/init.d/nlbwmon
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/nlbwmon.config $(1)/etc/config/nlbwmon
endef

$(eval $(call BuildPackage,nlbwmon))
