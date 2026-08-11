# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel xhci-mtk-hcd-v2)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/usb/usb_offload
ccflags-y += -I$(srctree)/sound/usb
ccflags-y += -I$(src)   # TRACE_INCLUDE_PATH . in xhci-trace.h

obj-$(CONFIG_DEVICE_MODULES_USB_XHCI_MTK) += xhci-mtk-hcd-v2.o
xhci-mtk-hcd-v2-y := xhci.o xhci-mem.o xhci-ext-caps.o xhci-ring.o \
	xhci-hub.o xhci-dbg.o xhci-trace.o xhci-mtk.o xhci-mtk-sch.o \
	quirks.o xhci-sideband.o
xhci-mtk-hcd-v2-$(CONFIG_DEBUG_FS) += xhci-debugfs.o
