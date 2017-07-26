include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-nlbwmon
PKG_VERSION:=2017-07-26
PKG_RELEASE:=1

PKG_MAINTAINER:=Jo-Philipp Wich <jo@mein.io>
PKG_LICENSE:=ISC

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-nlbwmon
	SECTION:=luci
	CATEGORY:=LuCI
	SUBMENU:=3. Applications
	TITLE:=Netlink based bandwidth accounting
	URL:=https://github.com/jow-/nlbwmon
	MAINTAINER:=Jo-Philipp Wich <jo@mein.io>
	DEPENDS:=+nlbwmon +luci-base
endef

define Package/luci-app-nlbwmon/description
	This package allows you to configure the netlink bandwidth monitor and
	display the traffic stats in LuCI.
endef

define Build/Compile
endef

define Package/luci-app-nlbwmon/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci
	$(CP) ./luasrc/* $(1)/usr/lib/lua/luci
endef

define Package/luci-app-nlbwmon/postinst
[ -n "$${IPKG_INSTROOT}" ] || {
	uci -q batch <<-EOF >/dev/null
		delete ucitrack.@nlbwmon[-1]
		add ucitrack nlbwmon
		set ucitrack.@nlbwmon[-1].init=nlbwmon
		commit ucitrack
	EOF

	rm -f /tmp/luci-indexcache
	exit 0
}
endef


$(eval $(call BuildPackage,luci-app-nlbwmon))
