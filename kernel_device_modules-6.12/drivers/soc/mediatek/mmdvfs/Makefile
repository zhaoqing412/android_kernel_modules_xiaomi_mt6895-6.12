# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdebug/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3/

# xaga: NOT built - mtk-mmdvfs-v5 is the mt6993-only implementation (alps
# mgk_64.bzl platform map). mt6895 uses mtk-mmdvfs-v3 (drivers/soc/mediatek/
# Makefile), which provides the same mmdvfs_get_mmup_base /
# mtk_mmdvfs_enable_vcp exports for mtk-mml / mmqos-common; building both
# duplicates those symbols -> "exports duplicate symbol" -> init kill
# (real-device panic after devapc fix).
# obj-$(CONFIG_MTK_TINYSYS_VCP_SUPPORT) += mtk-mmdvfs-v5.o

