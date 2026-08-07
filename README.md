# kernel_xiaomi_mt6895-6.12

xaga（Redmi Note 11T Pro / POCO X4 GT / Redmi K50i，Dimensity 8100 / MT6895）的 **Android 6.12 内核模块移植树**。

- 基座：OPPO 6.12 MTK 模块树（`kernel_device_modules-6.12`，kleaf/mgk 构建模型）
- 移植来源：小米 5.10 ESK 内核（`kernel_xiaomi_mt6895`，16.2-rebase）与官方 5.10 内核（xaga-s-oss）
- 内核版本：6.12（GKI common + MTK mgk 规则）
- 配套内核源码：OPPO 6.12 内核（`oddo6_12/android_kernel_oppo_mt6896`，6.12.23）

## 目录

- `kernel_device_modules-6.12/` — 模块树（本仓库主体，kleaf/BUILD.bazel + Kbuild 双路径）
- `BRINGUP.md` — 开机 bring-up 指南（构建步骤、上电顺序、充电/显示/触摸对齐）
- 本文件 — 仓库概览、已移植内容、已知缺口

## 构建

本树依赖完整的 MTK/AOSP 环境（kleaf `build/` 规则 + clang 预编译 + alps manifest 的 `vendor/mediatek` 等 sibling 项目），**本仓库单独无法构建**。构建配方要点（详见 BRINGUP.md §1）：

```
KCONFIG_EXT_PREFIX=<modules>/   # 每个 make/conf 都必须带，否则模块符号被丢弃
gki_defconfig + merge_config.sh -m <mgk_64_k612_defconfig> <vendor/xaga.config>
CONFIG_MODULE_SIG_KEY 需为绝对 pem 路径
```

板级 defconfig 片段：`arch/arm64/configs/vendor/xaga.config`
板级 DTS：`arch/arm64/boot/dts/mediatek/xaga.dts`（overlay 于 `mt6895.dts` SoC 基座）

## 已移植内容（自 5.10 ESK / 官方）

**驱动（均已注册进 kleaf 的 `kernel/kleaf/mgk_64.bzl` + BUILD.bazel）**：

| 模块 | 说明 |
|---|---|
| simtray | 卡托状态（gpiod 重写） |
| hwid | `/sys/hwid` 硬件标识 |
| xiaomi_touch + double_click | 小米触控类 + 双击唤醒 |
| NVT36672C | Novatek SPI 触摸屏（6.12 API 适配） |
| aw8697 haptic | Awinic 线性马达 |
| leds-ktz8863a + panel-l16 ×2 | 背光 + 两块 L16 DSC 面板（ESD 恢复） |
| ln8000 / sc8551 / sc8561 / bq28z610 | 充电泵 + 电量计（6.12 psy 对齐） |
| pd_cp_manager + 充电框架 | mtk_charger/mtk_pd_* 全套（`mtk-master-charger` psy 名对齐） |
| **6 颗 camera sensor** | s5khm2/s5k4h7/ov16a1/s5kgw1/gc02m1/ov02b10（src-v4l2，2026-08-07 移植） |
| **KTD2687 相机闪光灯** | 双灯驱动 + flashlight 核心接线（2026-08-07 移植） |
| **MTK Pump Express** | pep/pep20/pep40/pep45/pep50/pep50p 协议模块接线（2026-08-07 恢复） |

**板级 DTS**：`xaga-mt6895.dtsi` 链（charger/display/thermal/touch/camera）+ `cust/xaga.dtsi` + `xaga_global.dts`（全球版变体）。

## 特性

- **内核日志 → oops 分区**：`CONFIG_PSTORE_BLK=y` + `PSTORE_BLK_BLKDEV="/dev/block/sdc81"`（pstore/blk 写 oops 分区）
  - 注意：pstore/blk 的 `best_effort` 默认关，若 oops 分区未格式化需加 cmdline `pstore_blk.best_effort=1`
- 产物镜像（boot/vendor_boot/dtbo）：见 `xaga/images/6.12/`

## 已知缺口 / 待用户环境处理

1. **指纹驱动（goodix_cap）**：xaga DTS 有 `goodix,goodix-fp` 节点（`cust_mt6895_fingerprint.dtsi`），5.10 用 `drivers/input/fingerprint/goodix_cap/`（GF3626ZS9 TEE 驱动，`CONFIG_GOODIX_CAP_FINGERPRINT=m`）。**6.12 树未移植**，原因：驱动依赖 5.10 内核私有 SPI 头（`mtk_spi.h`/`mtk_spi_hal.h`，来自 5.10 的 `drivers/spi/mediatek/<plat>/`，不在本工作区）。移植步骤：
   - 从 5.10 拷 `drivers/input/fingerprint/goodix_cap/` + `fingerprint/Kconfig` + `fingerprint/Makefile`
   - 适配 `mtk_spi.h` 依赖（用户环境 5.10 SPI 头或改通用 SPI API）
   - `xaga.config` 加 `CONFIG_GOODIX_CAP_FINGERPRINT=m`；`drivers/input/Makefile` 加 `obj-$(CONFIG_GOODIX_CAP_FINGERPRINT) += fingerprint/`
2. **vendor/mediatek（mtkcam 等）**：`mgk_64.bzl` 引用 `//vendor/mediatek/...` 目标由用户完整 MTK 环境提供，本仓库不含
3. **camera sensor 合入**：6 颗 sensor 在本树 `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xaga*/`，用户环境需在 `src-v4l2/BUILD.bazel` 的 `config_cust_kernel_imgsensor` 追加 6 个名字（详见 BRINGUP.md §6）
4. **lm3644 注册残留**：`mgk_64.bzl:1361` 给 mt6895 注册了 lm3644（xaga 用 KTD2687 不用 LM3644，保留无害，可删）
5. **触控 fw**：nt36672c fw 文件需放入 vendor 分区对应路径

## 提交历史要点

- `3c365ef` 特性：oops 分区 pstore/blk + 充电框架接线
- `6fc39f0` / `132501c` 全构建修复（6.12 API 适配）
- `31e4114` 全面审计修复（crash bug/config 缺口/kleaf 注册）
- `9c6a13e` + `bc47032` + `edd496c` camera sensor / KTD2687 闪光灯 / Pump Express（2026-08-07）

## 许可

内核模块遵循各源仓库（OPPO/小米/MTK）的 GPL 许可。
