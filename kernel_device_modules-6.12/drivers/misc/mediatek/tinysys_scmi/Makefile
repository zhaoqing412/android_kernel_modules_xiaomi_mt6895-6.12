# SPDX-License-Identifier: GPL-2.0
# xaga: platform module wiring (get_scmi_tinysys_info / scmi_tinysys_common_set
# for mtk-mminfra-debug)
obj-$(CONFIG_MTK_TINYSYS_SCMI) += tinysys-scmi.o

tinysys-scmi-y := tinysys-scmi-drv.o tinysys.o


ccflags-y += -I$(srctree)/drivers/firmware/arm_scmi

ccflags-y += -DTINYSYS_SCMI_DEBUG
