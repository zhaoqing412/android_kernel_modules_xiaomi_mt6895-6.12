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

ifeq ($(CONFIG_MTK_GCOV_KERNEL),y)
GCOV_PROFILE := y
endif


# xaga: platform module wiring. alps 6.12 builds TWO modules here (see the
# aed/BUILD.bazel): aee_aed (aed-main + aed-debug + aee-common + slog, which
# EXPORTs aee_get_mode) and aee_rs (which depends on it). Shipping only
# aee_rs made init's first-stage module load fail with "Unknown symbol
# aee_get_mode" and abort boot (Attempted to kill init, 2026-08-10).
obj-m += aee_aed.o aee_rs.o
aee_aed-y := aed-main.o aed-debug.o aee-common.o slog.o
