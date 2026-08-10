# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel mcupm = mcupm_v3.ko;
# exports get_mcupm_ipidev for mtk_peak_power_budget)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mcupm/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mcupm/include/v3

obj-$(CONFIG_MTK_TINYSYS_MCUPM_SUPPORT) += mcupm_v3.o
mcupm_v3-y := common/mcupm_helper.o common/mcupm_timesync.o \
	common/mcupm_sysfs.o mcupm_driver.o mcupm_plt.o mcupm_ipi.o
