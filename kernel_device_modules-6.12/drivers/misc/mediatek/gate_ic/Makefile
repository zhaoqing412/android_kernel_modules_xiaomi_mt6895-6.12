#
# Copyright (C) 2015 MediaTek Inc.
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
# xaga: platform module wiring (alps: rt4831a_drv.o exports
# _gate_ic_backlight_set for leds-mtk-disp)
obj-$(CONFIG_RT4831A_I2C) += rt4831a_drv.o
ccflags-$(CONFIG_RT4831A_I2C) += -I$(DEVICE_MODULES_PATH)/drivers/leds/