# SPDX-License-Identifier: GPL-2.0
obj-m += phy-mtk-ufs.o

# xaga: platform module wiring
obj-m += phy-mtk-xsphy.o
# xaga: phy-mtk-tphy provides the USB2 (u2port0) / USB3 (u3port0) PHYs that
# mtu3's `phys = <&u2port0 PHY_TYPE_USB2>, <&u3port0 PHY_TYPE_USB3>` needs.
# Without it mtu3 defers/fails -> no UDC -> fastbootd gets
# "couldn't find an available UDC" and recovery auto-reboots (real-device,
# 2026-08-11). CONFIG_DEVICE_MODULES_PHY_MTK_TPHY=m is set by mgk defconfig
# (alps wires it via BUILD.bazel). Hardcoded obj-m like the siblings - the
# DEVICE_MODULES_ Kconfig symbol does not drive the OOT M= build.
obj-m += phy-mtk-tphy.o
