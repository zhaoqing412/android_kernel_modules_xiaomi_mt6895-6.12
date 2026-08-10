# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel mtu3; exports ssusb_get_drvdata
# / ssusb_power_*_notifier for ps5170)
ccflags-y += -I$(srctree)/drivers/usb/gadget/function
ccflags-y += -I$(srctree)/drivers/usb/typec
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/

obj-$(CONFIG_DEVICE_MODULES_USB_MTU3) += mtu3.o
mtu3-y := mtu3_core.o mtu3_dr.o mtu3_gadget_ep0.o mtu3_gadget.o \
	mtu3_plat.o mtu3_host.o mtu3_qmu.o
mtu3-$(CONFIG_TRACING) += mtu3_trace.o
mtu3-$(CONFIG_DEBUG_FS) += mtu3_debugfs.o
