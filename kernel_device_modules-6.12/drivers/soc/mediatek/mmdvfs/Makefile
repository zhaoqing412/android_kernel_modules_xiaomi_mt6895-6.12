# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdebug/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3/

# xaga: platform module wiring (exports mtk_mmdvfs_enable_vcp for
# mtk-mml / mmqos-common)
obj-$(CONFIG_MTK_TINYSYS_VCP_SUPPORT) += mtk-mmdvfs-v5.o

