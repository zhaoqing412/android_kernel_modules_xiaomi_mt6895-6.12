# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat

ifneq ($(filter y m, $(CONFIG_MTK_MME_SUPPORT)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mme
endif

obj-$(CONFIG_DRM_PANEL_NT37801_MSYNC2_RTE) += panel-nt37801-msync2-rte.o
obj-$(CONFIG_DRM_PANEL_BOE_TV106C9MLL0) += ocp2138_i2c.o
obj-$(CONFIG_DRM_PANEL_MAXIAM_MAX96851) += panel-maxiam-max96851.o
obj-$(CONFIG_DRM_PANEL_AC160_P_3_A0004_CMD_PANEL) += AC160_P_3_A0004_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_AC160_P_7_A0001_CMD_PANEL) += AC160_P_7_A0001_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_AC124_P_3_A0004_CMD_PANEL) += ac124_p_3_a0004_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_AC124_P_B_A0012_CMD_PANEL) += ac124_p_b_a0012_cmd_panel.o
obj-$(CONFIG_DRM_PANEL_L16_42_02_0A_DSC_VDO) += panel-l16-42-02-0a-dsc-vdo.o
obj-$(CONFIG_DRM_PANEL_L16_36_02_0B_DSC_VDO) += panel-l16-36-02-0b-dsc-vdo.o
obj-$(CONFIG_DRM_PANEL_LEDS_KTZ8863A) += leds-ktz8863a.o
obj-$(CONFIG_DRM_PANEL_LEDS_KTZ8863A) += leds-ktz8863a.o

ccflags-$(CONFIG_DRM_MEDIATEK_PANEL) += -I$(DEVICE_MODULES_PATH)/drivers/leds/
