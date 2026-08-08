# xaga 6.12 移植状态总览

> **状态基准**: commit `270467e`（2026-08-07，分支 `main`，24 commits，工作树干净）
> **注意**: 提交历史被 rebase 重写过，所有旧文档引用的 hash 均已失效；新旧对照见 §9。

## 1. 项目定位与树结构

xaga（Redmi Note 11T Pro / POCO X4 GT / Redmi K50i，MT6895 / Dimensity 8100）的 **Android 6.12 内核模块移植树** —— 把小米 5.10 ESK 的板级 DTS 与 OOT 驱动搬到 6.12 MTK 模块树（`kernel_device_modules-6.12`，kleaf/mgk 构建模型，基座为 OPPO oddo6_12）。

```
xaga/kernel_xiaomi_mt6895-6.12/
├── STATUS.md / BRINGUP.md / README.md   # 状态总览 / bring-up 指南 / 概览
└── kernel_device_modules-6.12/
    ├── arch/arm64/boot/dts/mediatek/          # xaga*.dts 板级链（overlay mt6895.dts）
    ├── arch/arm64/configs/vendor/xaga.config  # defconfig 片段
    ├── drivers/...                            # 移植的小米 OOT 驱动
    ├── kernel/kleaf/mgk_64.bzl                # mgk 模块注册表
    └── vendor/mediatek/...                    # sensor 源 + MTK 平台模块
```

**本树不含**（需用户完整 MTK/AOSP 环境）：GKI common 内核（OPPO 6.12.23）、`kernel/build` mgk bazel 规则、clang 工具链、完整 `vendor/mediatek`。本仓库单独无法构建。

参考基线（工作区 `xaga/baselines/`）：内核参考基线 = `baselines/kernel/`（offical/xagaforge/ESK）；内核模块参考基线 = `baselines/modules/`（alps 6.12 / redmi 5.10 GKI）+ `baselines/kernel/xagaforge`（兼任）。

## 2. 移植进度总览

| 类别 | 状态 | 说明 |
|---|---|---|
| 小米 OOT 驱动（22 个对象） | ✅ 已完成 | 全部注册 mgk_64.bzl + BUILD.bazel，-Werror 编译干净 |
| MTK 平台模块 | ✅ 已完成（本机构建） | 123 ko（1409327 首发 118 + 5829818 补齐） |
| 相机 sensor ×6 | ✅ 已完成（语法验证） | src-v4l2；需用户环境 BUILD.bazel 追加 6 名（§6.3） |
| KTD2687 闪光灯 + flashlight 接线 | ✅ 已完成 | drivers/misc/mediatek/flashlight/（e321232/1aadba1） |
| MTK Pump Express | ✅ 已完成 | mtk_pep*（pep/pep20/pep40/pep45/pep50/pep50p，4b2d268） |
| 板级 DTS 链 | ✅ 已完成（静态验证） | 210 label 引用全解析、cpp-preprocess 干净；**⚠️ DTS Makefile 0 处 xaga 注册**，DTBO 列表须在用户环境 mgk 规则补（§6.4） |
| defconfig（xaga.config） | ✅ 已完成 | 关键符号见 §3 |
| 完整构建 | ✅ 本机可构建 | 产物见 §4；用户环境需重跑完整集成 |
| 指纹 goodix_cap | ❌ 未做 | 依赖 5.10 私有 mtk_spi.h，GF3626ZS9 TEE（§6.1） |
| 真机验证 | ⏳ 未做 | 充电流程 / xaga_global 变体 / 模块加载顺序（§6.6） |

## 3. 配置（xaga.config 关键符号）

| 符号 | 值 | 说明 |
|---|---|---|
| XMEXT_LN8000 / SC8551A_CHG_PUMP | =m | 双充电泵 ln8000/sc8551 |
| XMEXT_TI_GAUGE | =m | bq28z610 电量计（"bms" psy） |
| XM_PD_MANAGER | =m | 充电算法管理器 |
| DRM_PANEL_L16_* / DRM_PANEL_LEDS_KTZ8863A | =m | 两块 L16 DSC 面板 + 背光 |
| MI_DISP_ESD_CHECK | =y | 显示 ESD 恢复 |
| TOUCHSCREEN_NVT36672C_HOSTDL_SPI | =m | Novatek SPI 触摸 |
| TOUCHSCREEN_XIAOMI_TOUCHFEATURE / DOUBLE_CLICK | =m | 触控类 + 双击唤醒 |
| SIMTRAY_STATUS / MI_HARDWARE_ID | =m | 卡托 / /sys/hwid |
| INPUT_AW8697_HAPTIC | =m | 线性马达 |
| MTK_VIDEO_KTD2687 | =m | 闪光灯 |
| CUSTOM_KERNEL_IMGSENSOR | 6 颗 | sensor 名列表（s5khm2/s5k4h7/ov16a1/s5kgw1/gc02m1/ov02b10） |
| XAGA_OOPS_LOG | =m | oops 分区日志（自 5.10 移植的 kmsg_dumper，panic 安全 bio 直写 raw 分区 sdc81，无需 cmdline） |

## 4. 构建状态

**产物**（2026-08-07 本机）：`Image`（32MB）+ 22 个移植驱动 `.ko` + 123 个 MTK 平台模块 `.ko` + `xaga.dtbo` + `xaga_global.dtbo`。

**模块覆盖（官方 5.10 ramdisk 198 个模块 → 6.12，2026-08-08 实解包对比）**：官方 198 个 5.10 版 .ko（vermagic 5.10.198）在 6.12 内核上无法加载，由 6.12 侧以四层方式完整覆盖，**无硬缺口**：
- ① 同名直接替代（109 个）：123 打包模块中 109 个与官方同名（bq28z610/mtk_wdt/phy-mtk-ufs/pinctrl-mt6895 等）
- ② 更名/合并替代：clk-chk→clkchk、pinctrl-mtk-v2→pinctrl-mtk-common-v2、mt6375-battery→mt6375-gauge、mtk_mm_heap→mtk_system_heap、fan53870→fan53870-ldo、wl2868c→wl2868c-regulator、emi 系列→memory/mediatek、mtk_pep*（= mtk_pe*.o 组合）
- ③ 内核内置（约 30 个，无需 .ko）：mediatek-drm*（DRM_MEDIATEK_V2=y）、mtk-mmc-autok（mtk-mmc.c 内置）、regmap-spmi/reboot-mode/zsmalloc/system_heap、industrialio/kfifo_buf/mac80211/cfg80211（上游）
- ④ 非必需省略：aee/mrdump/iommu_debug/mmprofile 等调试诊断类；**唯一无对应物 = mi-memory**（小米私有，三树皆无，非启动必需）

⚠️ **123 是打包子集**：官方 198 中未打包的模块（aee/emi/dramc/tcpc/ufs/devapc/swpm 等约 75+ 个）在移植树（alps 同步）**有 6.12 源码**，用户环境按 mgk_64.bzl 全量构建（896 注册目标）即可覆盖；123 只是 8-07 快照的打包选择。

**已知 modpost undefined（非阻断）**：vendor/mediatek 模块的跨模块引用 MTK typec/tcpc（`tcpm_*`、`tcpc_dev_*`）——由用户环境完整依赖树 + depmod 在加载时解决。

**构建配方要点**（易错，详见 BRINGUP.md §1）：
- 每个 `make` 必须带 `KCONFIG_EXT_PREFIX=<modules>/`，否则 syncconfig 丢弃模块符号
- 配置合并用 `merge_config.sh -m`（仅追加语义；KCONFIG_ALLCONFIG 不被该 conf 支持）
- `CONFIG_MODULE_SIG_KEY` 必须是绝对 pem 路径
- 需恢复 OPPO 树缺失的经典 Kbuild 接线（顶层 obj-y += drivers/misc + drivers/input/misc；power/supply/Makefile 的 MTK_CHARGER 框架行；panel Makefile mediatek_v2 include 路径）

**编译中发现的真实 6.12 API 差异（已修复）**：i2c probe 1 参、remove→void、FW_ACTION_HOTPLUG 移除、GPIOF_DIR_IN→GPIOF_IN、devm_gpio_free→gpio_free、of_get_named_gpio_flags 缺失、spi_device.master 移除、MTK_PD_CONNECT enum 去重（6.12 的 mtk_pd_connect_type 提升到 adapter_class.h）、bq28z610 time_init→fg_time_init + night_charging 弃用、pd_cp_manager 辅助函数改文件级 static、补 vmalloc/pinctrl include。

## 5. 已移植驱动清单

| 分组 | 驱动 | 路径 |
|---|---|---|
| 输入/杂项 | simtray（gpiod 重写）、hwid、double_click、xiaomi_touch、NVT36672C（nt36xxx SPI）、aw8697_haptic | `drivers/misc/simtray.c`、`drivers/misc/hwid/`、`drivers/input/double_click.c`、`drivers/input/xiaomi/`、`drivers/input/touchscreen/NVT36672C/`、`drivers/input/misc/aw8697_haptic/` |
| 充电/电源 | ln8000、sc8551、sc8561、bq28z610、pd_cp_manager、pmic_voter、charger_class、adapter_class、mtk_charger 框架全套、mtk_pd_adapter、mtk_chg_type_det、MTK Pump Express（mtk_pep*） | `drivers/power/supply/` |
| 显示 | panel-l16-42-02-0a / -36-02-0b-dsc-vdo、leds-ktz8863a 背光 | `drivers/gpu/drm/panel/` |
| 相机 | 6 颗 sensor（s5khm2/s5k4h7/ov16a1/s5kgw1/gc02m1/ov02b10）、KTD2687 闪光灯 | `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xaga*/`、`drivers/misc/mediatek/flashlight/` |
| MTK 平台模块 | 123 ko | `drivers/misc/mediatek/` + `vendor/mediatek/` |

**DTS**：`xaga.dts`、`xaga_global.dts`（全球版变体）、`xaga-mt6895.dtsi` 链（touch/camera/charger/display/thermal）、`cust/xaga.dtsi`。

## 6. 已知缺口 / 风险

| # | 缺口 | 状态 | 处理 |
|---|---|---|---|
| 1 | 指纹 goodix_cap 未移植 | 阻塞指纹功能 | 依赖 5.10 内核私有 `mtk_spi.h`/`mtk_spi_hal.h`（用户环境），GF3626ZS9 TEE；移植步骤见 README.md 已知缺口 §1 |
| 2 | vendor/mediatek 完整集不在本树 | 需用户环境 | mtkcam 等由用户 MTK manifest 提供 |
| 3 | sensor 合入 | 需用户环境 | 在 `src-v4l2/BUILD.bazel` 的 `config_cust_kernel_imgsensor` 追加 6 个 xaga* 名字（make 路径自动读 CONFIG_CUSTOM_KERNEL_IMGSENSOR，无需改） |
| 4 | DTS Makefile 0 处 xaga 注册 | 需用户环境 | DTBO 列表在 `kernel/build` mgk 规则注册（本树无法完成）；mgk_64.bzl:1361 给 mt6895 注册了 lm3644（xaga 用 KTD2687，保留无害可删） |
| 5 | 触控 fw 文件 | 需设备侧 | nt36672e fw 文件放入 vendor 分区对应路径 |
| 6 | 真机验证未做 | 待设备 | psy 对齐后充电流程、xaga_global 变体、mtk-master-charger kABI/模块加载顺序 |
| 7 | xaga_oops_log 模块未在完整构建环境编译 | 需用户环境 | 新移植模块（2026-08-08），API 已对照 6.12.23 核实，语法检查通过（缺 generated 头）；需完整 MTK 环境编译验证；panic 下 IRQ 全屏蔽时 bio 完成中断不触发，已加有界轮询（超时放弃不挂死，写请求已入队仍会落盘） |

## 7. 下一步

1. **用户环境集成（阻塞真机）**：
   - mgk 规则把 `xaga` 加入项目 DTBO 列表
   - 合入 vendor/mediatek 及 alps sibling 项目
   - sensor 追加（§6.3）
   - 完整构建 + depmod 打包
2. **真机 bring-up**（顺序见 BRINGUP.md §3.4）：probe 检查 → `/sys/class/power_supply/` 应有 mtk-master-charger/bms/battery → 插 5V 普通充电 → 插 PD 快充验证管理器。
3. **剩余工作（非阻塞）**：指纹（可选）、lm3644 清理（可选）、触控 fw 放置。

## 8. 决策记录要点

| 决策 | 理由 |
|---|---|
| 基于 OPPO oddo6_12 而非小米 6.6 | 同版本（6.12）优先；小米 6.6 是 GKI common，无 MTK 设备层 |
| qc_cp_manager 不启用 | xaga 是 MTK PD 快充设备；5.10 vendor 配置从未设置 XM_QC_MANAGER |
| 触摸用 NVT36672C 而非 6.12 NT36532 | xaga 实际出货驱动（双击唤醒 + 游戏参数） |
| C7 不移植 fpsgo_cus/msync2_frd_cus | fpsgo 被 6.12 fpsgo_v3 完整覆盖；msync2 核心是闭源 5.10 二进制 |
| 保留 of_gpio.h/of_get_named_gpio | 63 个树内用户；移植更多 5.10 驱动时勿"现代化"成 gpiod |
| oops 日志用自定义 kmsg_dumper（xaga_oops_log）而非官方 pstore/blk | pstore/blk 的 best_effort 模式 panic 无法落盘、需 cmdline 注入且 zone 布局复杂；自定义模块轮询 bio 直写 raw 分区，panic 安全、读取即文本（审计 2026-08-08 定案，原 0b7a811 PSTORE_BLK 方案弃用） |

## 9. 新旧 commit 对照（rebase 后旧 hash 全部失效）

| 旧（失效） | 新（2026-08-07） | 内容 |
|---|---|---|
| bf0eb34 | 49dfceb | 导入 6.12 modules + xaga 板级移植 |
| 8de91e2 | 19311e8 | 小米充电框架 + panel providers |
| 14b999c | 3c16b0a | xaga_global 变体 + 启用 charge-pump |
| — | f4bc147 | NVT36672C SPI 触摸 |
| 26f455e | 109b0d0 | usb_psy 对齐 mtk-master-charger |
| — | 59499d6 | Kconfig.ext 链修复 |
| — | d94a22c | 不启用 qc_cp_manager |
| 132501c | e361186 | 首次编译 6.12 API 修复 |
| 6fc39f0 | d55ac4f | 全构建修复 |
| — | 0b7a811 | pstore/blk oops 日志 |
| — | 1409327 / 5829818 | 全量平台模块 118 / 123 ko |
| — | a377ba2 / 4f9489f | 审计修复（crash bug / config 缺口 / kleaf 注册） |
| — | 11690c7 | 清理 OPPO 配置残留 |
| 9c6a13e 等 | 7f75af8 / f3e8e80 | 6 颗 camera sensor + 文档 |
| — | e321232 / 1aadba1 | KTD2687 闪光灯 + flashlight 接线 |
| — | 4b2d268 | MTK Pump Express |
| — | 47e0ac5 / 270467e | README + 指纹缺口文档 / 设备名修正 |

## 10. 相关文档索引

| 文档 | 角色 | 何时看 |
|---|---|---|
| **STATUS.md（本文件）** | 状态总览 | 问"移植到什么程度/还差什么" |
| BRINGUP.md | bring-up 指南 | 构建配方、上电顺序、充电对齐、sensor 合入步骤 |
| README.md | 仓库概览 | 快速了解树结构、特性、提交要点 |
