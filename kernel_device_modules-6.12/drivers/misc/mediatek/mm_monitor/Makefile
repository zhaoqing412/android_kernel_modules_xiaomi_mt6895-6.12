# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel mtk-mm-monitor-controller +
# mm-monitor-platform-mt6895; exports mtk_mmmc_* for mmqos-common)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/iommu/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(src)

obj-m += mtk-mm-monitor-controller.o
obj-m += mm-monitor-platform-mt6895.o
