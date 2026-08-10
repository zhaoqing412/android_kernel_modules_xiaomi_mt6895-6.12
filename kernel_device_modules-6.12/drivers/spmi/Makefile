# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for kernel SPMI framework.
#
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/

# xaga: platform module wiring
obj-m += spmi-mtk-mpu.o

# xaga: platform module wiring (alps BUILD.bazel: spmi-mtk-pmif =
# pmif-core.c + spmi-mtk-debug.c; exports ext_pmif_base for spmi-mtk-mpu)
obj-m += spmi-mtk-pmif.o
spmi-mtk-pmif-y := spmi-mtk-pmif-core.o spmi-mtk-debug.o
