#
# Copyright (C) 2008-2010 OpenWrt.org
# Copyright (C) 2016 Antony Chazapis <chazapis@gmail.com>
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=eoax
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/eoax
  SUBMENU:=Other modules
  TITLE:=Ethernet over AX.25 encapsulation
  DEPENDS:=+kmod-ax25
  FILES:=$(PKG_BUILD_DIR)/eoax.ko
  AUTOLOAD:=$(call AutoLoad,90,eoax,1)
  KCONFIG:=
endef

define KernelPackage/eoax/description
  Kernel module to encapsulate AX.25 in Ethernet frames.
  Use it to run Ethernet protocols over AX.25 links.
endef

EXTRA_KCONFIG:= \
	CONFIG_EOAX=m

EXTRA_CFLAGS:= \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=m,%,$(filter %=m,$(EXTRA_KCONFIG)))) \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=y,%,$(filter %=y,$(EXTRA_KCONFIG)))) \

MAKE_OPTS:= \
	ARCH="$(LINUX_KARCH)" \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	SUBDIRS="$(PKG_BUILD_DIR)" \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	$(EXTRA_KCONFIG)

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(MAKE_OPTS) \
		modules
endef

$(eval $(call KernelPackage,eoax))
