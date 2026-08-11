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

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdebug/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/

# xaga: mtk-mmdebug-vcp NOT built - the real VCP implementation duplicates the
# mmdebug_is_init_done / mtk_mmdebug_status_dump_register_notifier exports of
# mtk-mmdebug-vcp-stub (same header API), and in alps only the stub is
# referenced by ko_deps (mmdvfs-debug / mtk-mmdvfs-v3). Loading both ->
# "exports duplicate symbol" -> init kill. The stub is the mt6895 variant
# (real vcp needs vcp core + IPI infra).
obj-m += mtk-mmdebug-vcp-stub.o
# obj-m += mtk-mmdebug-vcp.o

