# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel: mtk_iommu_util /
# iommu_secure / iommu_debug). Parent drivers/misc/mediatek/Makefile must
# descend into iommu/ (obj-$(CONFIG_MTK_IOMMU_MISC)).
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3

obj-$(CONFIG_MTK_IOMMU_MISC) += iommu-secure.o

# alps module names differ from their main sources to avoid Kbuild
# self-aggregation: iommu_debug = iommu-debug.c + mtk-smmu-ela.c
obj-$(CONFIG_MTK_IOMMU_MISC_DBG) += iommu_debug.o mtk-iommu-util.o
iommu_debug-y := iommu-debug.o mtk-smmu-ela.o
