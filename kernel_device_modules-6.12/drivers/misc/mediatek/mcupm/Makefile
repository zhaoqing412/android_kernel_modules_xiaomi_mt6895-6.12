# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (alps BUILD.bazel mcupm; exports
# get_mcupm_ipidev / get_mcupms_ipidev_number for mtk_peak_power_budget)
obj-$(CONFIG_MTK_TINYSYS_MCUPM_SUPPORT) += v3/

ifneq (, $(filter $(strip $(CONFIG_MTK_PLAT_POWER_6893)),m y))
    obj-y += v1/
endif
