# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/hwccf/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vmm/inc
# xaga: platform module wiring (exports mtk_vmm_ctrl_dbg_use for mtk-mmdvfs-v5)
ifneq ($(wildcard $(src)/mtk-vmm-notifier.c),)
obj-$(CONFIG_MTK_VMM_NOTIFIER) += mtk-vmm-notifier.o
endif
