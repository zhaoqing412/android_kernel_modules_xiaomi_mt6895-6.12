#
# Copyright (C) 2017 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

#ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/
#ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/mach/
#ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/thermal/$(MTK_PLATFORM)/inc/

# ifeq ($(CONFIG_MTK_TINYSYS_SSPM_V3),y)
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sspm/v3
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tinysys_scmi
# endif
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pbm/
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpufreq/
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpueb/
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cg_ppt/
# ifneq ($(strip $(CONFIG_MTK_MDPM)),)
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mdpm/
# else
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mdpm_v1/inc/
# endif
# ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
# obj-$(CONFIG_MTK_PEAK_POWER_BUDGET) += mtk_peak_power_budget.o

# xaga: platform module wiring (alps BUILD.bazel; exports kicker_ppb_request_power
# for flashlight)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mcupm/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mdpm_v1/inc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpueb
ccflags-y += -I$(srctree)/drivers/gpu/mediatek/gpufreq
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpufreq/v2/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cg_ppt
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/tinysys_scmi
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mbraink
ccflags-y += -I$(src)
obj-$(CONFIG_MTK_PEAK_POWER_BUDGET) += mtk_peak_power_budget.o

# xaga: platform module wiring
obj-m += mtk_pbm.o
