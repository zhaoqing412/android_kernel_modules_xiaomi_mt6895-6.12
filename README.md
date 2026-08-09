# kernel_xiaomi_mt6895-6.12

> 移植状态总览见 [STATUS.md](STATUS.md)（基准 commit `270467e`，2026-08-07；历史经 rebase，旧 hash 见 STATUS.md §9）

xaga（Redmi Note 11T Pro / POCO X4 GT / Redmi K50i，Dimensity 8100 / MT6895）的 **Android 6.12 内核模块移植树**。

- 基座：OPPO 6.12 MTK 模块树（`kernel_device_modules-6.12`，kleaf/mgk 构建模型）
- 移植来源：小米 5.10 ESK 内核（`../baselines/kernel/kernel_xiaomi_mt6895`，16.2-rebase）与官方 5.10 内核（`../baselines/kernel/offical_kernel_xiaomi_mt6895`，xaga-s-oss）
- 内核版本：6.12（GKI common + MTK mgk 规则）
- 配套内核源码：OPPO 6.12 内核（`../android_kernel_oddo_mt6895`，6.12.23）

## 目录

- `kernel_device_modules-6.12/` — 模块树（本仓库主体，kleaf/BUILD.bazel + Kbuild 双路径）
- `STATUS.md` — 移植状态总览（进度/构建/缺口/下一步）
- `BRINGUP.md` — 开机 bring-up 指南（构建步骤、上电顺序、充电/显示/触摸对齐）
- 本文件 — 仓库概览、已移植内容、已知缺口

## 构建

本树依赖完整的 MTK/AOSP 环境（kleaf `build/` 规则 + clang 预编译 + alps manifest 的 `vendor/mediatek` 等 sibling 项目），**本仓库单独无法构建**。构建配方要点（详见 BRINGUP.md §1）：

```
KCONFIG_EXT_PREFIX=<modules>/   # 每个 make/conf 都必须带，否则模块符号被丢弃
gki_defconfig + merge_config.sh -m <mgk_64_k612_defconfig> <vendor/xaga.config>
CONFIG_MODULE_SIG_KEY 需为绝对 pem 路径
```

### 一键构建+打包：`./build.sh`

在完整工作区（OPPO 6.12 内核 `../android_kernel_oddo_mt6895`）内，`build.sh` 默认从零完成**全量编译 + 打包**（工具链/依赖处理参考 `clang_build_fix.sh`）：

1. 配置：`gki_defconfig` + `mgk_64_k612_defconfig` + `vendor/xaga.config`
2. 内核 `Image` + `Image.gz`（gzip）
3. in-tree 模块（刷新 `Module.symvers`）
4. OOT 模块 → **133 个 `.ko`**（`make M=`，硬断言）
5. DTS 三件：`mt6895.dtb`（SoC 基座）+ `xaga.dtbo` / `xaga_global.dtbo`（fdtoverlay 校验）
6. `images/out/boot_new.img` — magiskboot `-n`：Image.gz 内核 + 6.12 kernelsu 注入官方 boot
7. `images/out/vendor_boot_new.img` — mkbootimg：官方 vendor ramdisk 换 133 个 6.12 .ko + 元数据重建（modules.load/recovery 133 行 + depmod 平铺）+ DTBO_TAG dtb 槽 + pad 64MB
8. `images/out/dtbo_new.img` — DTOv1 单条目（xaga.dtbo，不 pad）

```
./build.sh                        # 默认：全量编译 + 打包（产物 → images/out/）
./build.sh --modules-only         # 只编译内核模块（配置 → 133 个 .ko），不编译 Image/DTS、不打包
./build.sh --no-clean             # 增量编译（不清除已有产物）
./build.sh --skip=BOOT,VENDOR     # 全量编译但跳过指定打包步骤（DTBO 同理）
./build.sh --help
```

产物：

- 编译产物在 `out/`：`Image` / `Image.gz` / `Module.symvers` / `dts/`（DTS 三件副本）
- 打包产物在 `images/out/`：`boot_new.img` / `vendor_boot_new.img` / `dtbo_new.img`
- 模块树内 **133 个 `.ko`**（`kernel_device_modules-6.12/` 原位）

**路径全部默认相对脚本目录，无需硬编码**：

- 模块树 `M` = `./kernel_device_modules-6.12`
- 编译输出 `OUT` = `./out`
- 官方镜像 `OFFICIAL` = `../images/offical/`（`boot.img` / `vendor_boot.img` / `dtbo.img`）
- 打包输出 `OUT_IMG` = `../images/out/`
- 工具：`MAGISKBOOT` / `MKBOOTIMG` / `MKDTBO` / `KERNELSU` 均在 `../images/building/tools/`
- 签名 key `PEM` = `$M/certs/mtk_signing_key.pem`
- 工具链 `LLVM_PREFIX` = `$HOME/clang`（**AOSP clang-r536225**，OPPO `build.config.constants` 推荐，勿用 apt 的 clang-18）；`USE_CCACHE=1` 可加 ccache 前缀

**OPPO 6.12 内核源码（`K`）自动定位**——依次尝试：

1. `K` 环境变量（若已设置且存在）
2. 脚本同级/父级目录：`../android_kernel_oddo_mt6895`（xaga/ 下，本机位置）等
3. GitHub 远程克隆名 `android_kernel_oddo_mt6895`（同级/父级/`xaga/` 下）
4. 都找不到时提示手动输入

所有路径均可用环境变量覆盖（`K`/`M`/`OUT`/`XAGA`/`IMG_DIR`/`OFFICIAL`/`OUT_IMG`/`PEM`/`LLVM_PREFIX`/`JOBS`）。编译日志保留在 `/tmp/xaga_build.*/`。

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
| ln8000 / sc8551 / sc8561 / bq28z610 | 充电泵 + 电量计（6.12 psy 对齐，commit 109b0d0） |
| pd_cp_manager + 充电框架 | mtk_charger/mtk_pd_* 全套（`mtk-master-charger` psy 名对齐） |
| **6 颗 camera sensor** | s5khm2/s5k4h7/ov16a1/s5kgw1/gc02m1/ov02b10（src-v4l2，2026-08-07 移植） |
| **KTD2687 相机闪光灯** | 双灯驱动 + flashlight 核心接线（2026-08-07 移植） |
| **MTK Pump Express** | pep/pep20/pep40/pep45/pep50/pep50p 协议模块接线（2026-08-07 恢复） |
| **MTK 平台模块（123 ko）** | 全量平台模块构建（1409327/5829818，自 alps 树同步） |

**板级 DTS**：`xaga-mt6895.dtsi` 链（charger/display/thermal/touch/camera）+ `cust/xaga.dtsi` + `xaga_global.dts`（全球版变体）。

## 特性

- **挂死定位（xaga-marker）**：`CONFIG_XAGA_MARKER_WRITER=m`（xaga-marker-writer 模块，modules.load 排第一），模块加载逐条写 XAGR 环到 minirdump 保留区 0x48170000；内核挂死（kmsg_dumper 不触发）时环最后一条 = 挂死模块，WDT 复位后 DRAM 保留，由 **lineage_xaga 内核的 xaga-marker 读端**下次启动 dmesg 打印（替代已弃用的 xaga_oops_log/oops 分区方案，2026-08-09）
- 产物镜像（boot/vendor_boot/dtbo）：见 `xaga/images/out/`（打包产物；构建中间产物在 `xaga/images/building/`）

## 已知缺口 / 待用户环境处理

1. **指纹驱动（goodix_cap）**：xaga DTS 有 `goodix,goodix-fp` 节点（`cust_mt6895_fingerprint.dtsi`），5.10 用 `drivers/input/fingerprint/goodix_cap/`（GF3626ZS9 TEE 驱动，`CONFIG_GOODIX_CAP_FINGERPRINT=m`）。**6.12 树未移植**，原因：驱动依赖 5.10 内核私有 SPI 头（`mtk_spi.h`/`mtk_spi_hal.h`，来自 5.10 的 `drivers/spi/mediatek/<plat>/`，不在本工作区）。移植步骤：
   - 从 5.10 拷 `drivers/input/fingerprint/goodix_cap/` + `fingerprint/Kconfig` + `fingerprint/Makefile`
   - 适配 `mtk_spi.h` 依赖（用户环境 5.10 SPI 头或改通用 SPI API）
   - `xaga.config` 加 `CONFIG_GOODIX_CAP_FINGERPRINT=m`；`drivers/input/Makefile` 加 `obj-$(CONFIG_GOODIX_CAP_FINGERPRINT) += fingerprint/`
2. **vendor/mediatek（mtkcam 等）**：`mgk_64.bzl` 引用 `//vendor/mediatek/...` 目标由用户完整 MTK 环境提供，本仓库不含
3. **camera sensor 合入**：6 颗 sensor 在本树 `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xaga*/`，用户环境需在 `src-v4l2/BUILD.bazel` 的 `config_cust_kernel_imgsensor` 追加 6 个名字（详见 BRINGUP.md §6）
4. **lm3644 注册残留**：`mgk_64.bzl:1361` 给 mt6895 注册了 lm3644（xaga 用 KTD2687 不用 LM3644，保留无害，可删）
5. **触控 fw**：nt36672c fw 文件需放入 vendor 分区对应路径
6. **真机验证未做**：psy 对齐后充电流程、xaga_global 变体、mtk-master-charger kABI/模块加载顺序（见 STATUS.md §6）

## 提交历史要点

- `49dfceb` 导入 6.12 modules + xaga 板级移植 / `19311e8` 充电框架 + panel providers / `f4bc147` NVT36672C 触摸
- `d55ac4f` / `e361186` 全构建与首次编译修复（6.12 API 适配）
- `1409327` + `5829818` 全量平台模块构建（118 → 123 ko）/ `a377ba2` 审计修复
- `7f75af8` + `f3e8e80` 6 颗 camera sensor / `e321232` + `1aadba1` KTD2687 闪光灯 / `4b2d268` Pump Express（2026-08-07）
- `270467e` README + 设备名修正；完整新旧 hash 对照见 STATUS.md §9
- `build.sh` 一键构建脚本（clean → 配置 → in-tree 模块 → 133 个 .ko）

## 许可

内核模块遵循各源仓库（OPPO/小米/MTK）的 GPL 许可。
