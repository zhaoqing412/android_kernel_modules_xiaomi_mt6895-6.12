# SPDX-License-Identifier: GPL-2.0

# xaga: platform module wiring (alps BUILD.bazel mmqos-common; exports
# mtk_mmqos_probe/remove for mmqos-mt6895)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mtk-interconnect
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/qos
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/soc/mediatek
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mm_monitor
ccflags-y += -I$(src)

obj-m += mmqos-common.o
mmqos-common-y := mmqos-mtk.o mmqos-hrt.o mmqos-test.o
mmqos-common-$(CONFIG_MTK_MMQOS_VCP) += mmqos-vcp.o mmqos-mmup.o

# xaga: platform module wiring
obj-m += mmqos-mt6895.o

# xaga: platform module wiring
obj-m += mtk-emi.o
