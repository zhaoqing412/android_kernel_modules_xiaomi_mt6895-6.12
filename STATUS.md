# xaga 6.12 移植状态总览

> **状态基准**: commit `30c80ea`（2026-08-13，分支 `main`，已推送 origin/main；此前 `a05b916` 基准的历史已 rebase，旧 hash 见 §9）
> **注意**: 提交历史被 rebase 重写过，所有旧文档引用的 hash 均已失效；新旧对照见 §9。
> **真机里程碑（2026-08-13）**: 显示链路根因闭环（PWM0/SPR0 compatible）后，**编译产物可进入 recovery 且显示正常**（不再 atomic oops / display size 0x0）。**进系统（正常 Android boot）需要 blob 支持**——本仓库只含内核 + GPL 模块源码，完整系统还需小米/MTK 专有 blob（TEE/gz/mcupm/sspm 固件、vendor 分区专有二进制如相机 HAL/指纹 TEE 应用等），见 §6.7。
> **真机现象（2026-08-13，仅描述未修复）**: **开机过程（LK→内核启动早期）屏幕会花屏，进入 recovery 后显示恢复正常**——花屏位于早期显示初始化阶段，不影响 recovery 内显示，暂不处理（非阻塞）。

## 1. 项目定位与树结构

xaga（Redmi Note 11T Pro / POCO X4 GT / Redmi K50i，MT6895 / Dimensity 8100）的 **Android 6.12 内核模块移植树** —— 把小米 5.10 ESK 的板级 DTS 与 OOT 驱动搬到 6.12 MTK 模块树（`kernel_device_modules-6.12`，kleaf/mgk 构建模型，基座为 OPPO `android_kernel_oddo_mt6895`）。

```
xaga/kernel_xiaomi_mt6895-6.12/
├── STATUS.md / BRINGUP.md / README.md / xaga-drm-restore.md / xaga-log-capture.md
│                                   # 状态总览 / bring-up / 概览 / DRM 依赖恢复 / 日志捕获
└── kernel_device_modules-6.12/
    ├── arch/arm64/boot/dts/mediatek/          # xaga*.dts 板级链（overlay mt6895.dts）
    ├── arch/arm64/configs/vendor/xaga.config  # defconfig 片段
    ├── drivers/...                            # 移植的小米 OOT 驱动
    ├── kernel/kleaf/mgk_64.bzl                # mgk 模块注册表
    └── vendor/mediatek/...                    # sensor 源 + MTK 平台模块
```

**本树不含**（需用户完整 MTK/AOSP 环境）：GKI common 内核（OPPO 6.12.23）、`kernel/build` mgk bazel 规则、clang 工具链、完整 `vendor/mediatek`。本仓库单独无法构建。

参考基线（工作区 `xaga/baselines/`）：内核参考基线 = `baselines/kernel/`（offical/xagaforge/lineage_xaga）；内核模块参考基线 = `baselines/modules/`（alps 6.12 / redmi 5.10 GKI）+ `baselines/kernel/xagaforge`（兼任）。

## 2. 移植进度总览

| 类别 | 状态 | 说明 |
|---|---|---|
| 小米 OOT 驱动（22 个对象） | ✅ 已完成 | 全部注册 mgk_64.bzl + BUILD.bazel，-Werror 编译干净 |
| MTK 平台模块 | ✅ 已完成（本机构建） | 123 ko（1409327 首发 118 + 5829818 补齐） |
| **MTK DRM 全依赖链** | ✅ **已完成（2026-08-10，扩至 200 ko）** | 0 undefined；mediatek_v2/mml/gpufreq/slbc/hwccf 等约 60 模块恢复，见 `xaga-drm-restore.md` |
| 相机 sensor ×6 | ✅ 已完成（语法验证） | src-v4l2；需用户环境 BUILD.bazel 追加 6 名（§6.3） |
| KTD2687 闪光灯 + flashlight 接线 | ✅ 已完成 | drivers/misc/mediatek/flashlight/（e321232/1aadba1） |
| MTK Pump Express | ✅ 已完成 | mtk_pep*（pep/pep20/pep40/pep45/pep50/pep50p，4b2d268） |
| 板级 DTS 链 | ✅ 已完成（静态验证） | 210 label 引用全解析、cpp-preprocess 干净；**⚠️ DTS Makefile 0 处 xaga 注册**，DTBO 列表须在用户环境 mgk 规则补（§6.4） |
| defconfig（xaga.config） | ✅ 已完成 | 关键符号见 §3 |
| 完整构建 + 打包 | ✅ 本机可构建 | 产物见 §4；用户环境需重跑完整集成 |
| 指纹 goodix_cap | ❌ 未做 | 依赖 5.10 私有 mtk_spi.h，GF3626ZS9 TEE（§6.1） |
| 真机验证 | ✅ **recovery 可进、显示正常（2026-08-13）** | 显示链路根因（PWM0/SPR0 compatible → crtc0 未创建）闭环后 recovery 正常进入；**进系统需 blob 支持（§6.7）** |

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
| XAGA_MARKER_WRITER | =m | boot-stage marker（XAGR 环写 log_store 0x7ffbf000（minirdump 触发 mrdump 重启），挂死定位；**读取 = 直接看 expdb 转储**，lineage_xaga 读端代码仅备用） |

## 4. 构建状态

**产物**（2026-08-11，本机 `./build.sh` 全量）：`Image.gz`（13.3MB）+ **193 个 OOT 模块 `.ko`** + `mt6895.dtb` / `xaga.dtbo` / `xaga_global.dtbo` + 三个可刷镜像（`boot_new.img` / `vendor_boot_new.img` / `dtbo_new.img`，见 §4a）。单次全量构建约 4 分钟（32 核），带时间戳/步骤计数/进度条输出（2026-08-11 起）。

### 4a. 打包集构成（2026-08-13 定型）

- **204 ko 入 vendor_boot = 200 OOT + 4 in-tree**：OOT 模块来自 M 树 `make M=`（200 个，build.sh 硬断言；2026-08-13 由 197 扩至 200，恢复 eMMC/缓存等提供者）；4 个 in-tree（K 树）模块是 OOT 的符号提供者、6.12 里是 `=m`（不编进 Image），必须随 vendor_boot 打包：
  - `drm_display_helper.ko`（`drm_dp_*`，供 mediatek-drm）→ `CONFIG_DRM_DISPLAY_HELPER=m`
  - `drm_dma_helper.ko`（`drm_gem_dma_vm_ops`）→ `CONFIG_DRM_GEM_DMA_HELPER=m`
  - `industrialio-triggered-buffer.ko` + `kfifo_buf.ko`（供 mt6375-adc）
- **模块裁剪（2026-08-11）**：4 对"双模块重复导出符号"冲突（真机 `exports duplicate symbol` → init kill）按 alps 平台映射各留其一：
  - `device-apc-common-legacy`（老 SoC v1 接口）去 / 留 `device-apc-common`（multi-ao）
  - `mtk-mmdvfs-v5`（mt6993 专属）去 / 留 `mtk-mmdvfs-v3`（mt6895）
  - `mtk-mmdebug-vcp`（真实现）去 / 留 `mtk-mmdebug-vcp-stub`（唯一被 ko_deps 引用）
  - `mcDrvModule-ffa`（FF-A 传输）去 / 留 `mcDrvModule`（官方 5.10 同款）
- **`mtk_drm_gateic` → `mediatek-drm-gateic`**（2026-08-11）：复合模块名必须与主源对象不同名（Kbuild 循环依赖），采用官方 5.10（xagaforge）与 alps mgk_64.bzl 名字；`-y` 列表含主文件 `mtk_drm_gateic.o`。

**模块覆盖（官方 5.10 ramdisk 198 个模块 → 6.12，2026-08-08/10 实解包对比）**：官方 198 个 5.10 版 .ko（vermagic 5.10.198）在 6.12 内核上无法加载，由 6.12 侧以四层方式完整覆盖，**无硬缺口**：
- ① 同名直接替代（109 个）：123 打包模块中 109 个与官方同名（bq28z610/mtk_wdt/phy-mtk-ufs/pinctrl-mt6895 等）
- ② 更名/合并替代：clk-chk→clkchk、pinctrl-mtk-v2→pinctrl-mtk-common-v2、mt6375-battery→mt6375-gauge、mtk_mm_heap→mtk_system_heap、fan53870→fan53870-ldo、wl2868c→wl2868c-regulator、emi 系列→memory/mediatek、mtk_pep*（= mtk_pe*.o 组合）
- ③ 内核内置（约 30 个，无需 .ko）：mediatek-drm*（DRM_MEDIATEK_V2=y）、mtk-mmc-autok（mtk-mmc.c 内置）、regmap-spmi/reboot-mode/zsmalloc/system_heap、industrialio/kfifo_buf/mac80211/cfg80211（上游）
- ④ 调试诊断类省略；**唯一无对应物 = mi-memory**（小米私有，三树皆无，非启动必需）

⚠️ **历史快照说明**：123 ko 是 2026-08-07 打包快照；2026-08-10 起打包集扩展为 197 ko（恢复 DRM/typec/gpufreq 等依赖链）；2026-08-11 定型为 193 OOT + 4 in-tree = 197（见 §4a）。

**modpost undefined**：**2026-08-10 起清零**。此前 vendor/mediatek 跨引用 MTK typec/tcpc（`tcpm_*`、`tcpc_dev_*`）已随 tcpc_class 模块恢复解决；全部 200 ko 经 `llvm-nm` 审计无 undefined。

**构建配方要点**（易错，详见 BRINGUP.md §1）：
- 每个 `make` 必须带 `KCONFIG_EXT_PREFIX=<modules>/`，否则 syncconfig 丢弃模块符号
- 配置合并用 `merge_config.sh -m`（仅追加语义；KCONFIG_ALLCONFIG 不被该 conf 支持）
- `CONFIG_MODULE_SIG_KEY` 必须是绝对 pem 路径
- 需恢复 OPPO 树缺失的经典 Kbuild 接线（顶层 obj-y += drivers/misc + drivers/input/misc；power/supply/Makefile 的 MTK_CHARGER 框架行；panel Makefile mediatek_v2 include 路径）；2026-08-10 又补齐父级下降（iommu/tinysys/vcp/mmqos/blocktag/usb/dma-buf-heaps/hal 等，详见 xaga-drm-restore.md §六）
- 打包阶段对 .ko 执行 `llvm-strip --strip-debug`（保留 .symtab/__ksymtab/modinfo）——官方 ko 无 DWARF（39.4MB），移植树带 DWARF5（130MB），strip 后 25MB（2026-08-10）

**编译中发现的真实 6.12 API 差异（已修复）**：i2c probe 1 参、remove→void、FW_ACTION_HOTPLUG 移除、GPIOF_DIR_IN→GPIOF_IN、devm_gpio_free→gpio_free、of_get_named_gpio_flags 缺失、spi_device.master 移除、MTK_PD_CONNECT enum 去重（6.12 的 mtk_pd_connect_type 提升到 adapter_class.h）、bq28z610 time_init→fg_time_init + night_charging 弃用、pd_cp_manager 辅助函数改文件级 static、补 vmalloc/pinctrl include。另有 2026-08-10 新增：cmdq-util.c kvm include 守卫、nt36xxx.c 桩化 get_lockdown_info_for_nvt、mtk_disp_recovery.c 换 alps 原版 + panel_dead 移植、mtk_dump.h 补声明。

## 5. 已移植驱动清单

| 分组 | 驱动 | 路径 |
|---|---|---|
| 输入/杂项 | simtray（gpiod 重写）、hwid、double_click、xiaomi_touch、NVT36672C（nt36xxx SPI）、aw8697_haptic | `drivers/misc/simtray.c`、`drivers/misc/hwid/`、`drivers/input/double_click.c`、`drivers/input/xiaomi/`、`drivers/input/touchscreen/NVT36672C/`、`drivers/input/misc/aw8697_haptic/` |
| 充电/电源 | ln8000、sc8551、sc8561、bq28z610、pd_cp_manager、pmic_voter、charger_class、adapter_class、mtk_charger 框架全套、mtk_pd_adapter、mtk_chg_type_det、MTK Pump Express（mtk_pep*） | `drivers/power/supply/` |
| 显示 | panel-l16-42-02-0a / -36-02-0b-dsc-vdo、leds-ktz8863a 背光 | `drivers/gpu/drm/panel/` |
| **DRM 框架链（2026-08-10 恢复）** | mediatek_v2（mediatek-drm 80+ 文件）、mtk_panel_ext/mtk_sync/mtk_disp_notify、mtk-mml、mtk_drm_gateic | `drivers/gpu/drm/mediatek/{mediatek_v2,mml,panel}/`（详见 xaga-drm-restore.md） |
| 相机 | 6 颗 sensor（s5khm2/s5k4h7/ov16a1/s5kgw1/gc02m1/ov02b10）、KTD2687 闪光灯 | `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xaga*/`、`drivers/misc/mediatek/flashlight/` |
| MTK 平台模块 | 200 ko（含 DRM 依赖链） | `drivers/misc/mediatek/` + `vendor/mediatek/` |

**DTS**：`xaga.dts`、`xaga_global.dts`（全球版变体）、`xaga-mt6895.dtsi` 链（touch/camera/charger/display/thermal）、`cust/xaga.dtsi`。

## 6. 已知缺口 / 风险

| # | 缺口 | 状态 | 处理 |
|---|---|---|---|
| 1 | 指纹 goodix_cap 未移植 | 阻塞指纹功能 | 依赖 5.10 内核私有 `mtk_spi.h`/`mtk_spi_hal.h`（用户环境），GF3626ZS9 TEE；移植步骤见 README.md 已知缺口 §1 |
| 2 | vendor/mediatek 完整集不在本树 | 需用户环境 | mtkcam 等由用户 MTK manifest 提供 |
| 3 | sensor 合入 | 需用户环境 | 在 `src-v4l2/BUILD.bazel` 的 `config_cust_kernel_imgsensor` 追加 6 个 xaga* 名字（make 路径自动读 CONFIG_CUSTOM_KERNEL_IMGSENSOR，无需改） |
| 4 | DTS Makefile 0 处 xaga 注册 | 需用户环境 | DTBO 列表在 `kernel/build` mgk 规则注册（本树无法完成）；mgk_64.bzl:1361 给 mt6895 注册了 lm3644（xaga 用 KTD2687，保留无害可删） |
| 5 | 触控 fw 文件 | 需设备侧 | nt36672e fw 文件放入 vendor 分区对应路径 |
| 6 | 功能验证（模块加载已通） | 进行中 | 200 ko 加载成功后：进系统 → `/sys/class/power_supply/` 应有 mtk-master-charger/bms/battery → 5V 普通充电 → PD 快充管理器验证（顺序见 BRINGUP.md §3.4；进系统需 blob 支持见 §6.8） |
| 7 | **启动模式 = recovery（boot mode 2）** | ✅ 已解决（2026-08-13） | 显示链路修复（PWM0/SPR0 compatible）后 **recovery 正常进入且显示正常**；正常重启进系统受 blob 限制（见下） |
| 8 | **进系统需要 blob 支持** | ⚠️ 当前边界 | **本仓库产物（内核 + GPL 模块）验证到 recovery 为止**。完整 Android 系统启动还需要**专有 blob**（非本仓库、不可重新分发）：TEE/gz/mcupm/sspm/pi_img 等固件镜像、vendor 分区专有二进制（相机 HAL/ISP、指纹 GF3626ZS9 TEE 应用、音频 DSP 固件等）、以及官方 vendor_boot 之外的 system/vendor 分区镜像。这些来自小米官方系统/MTK 发布包，需用户在完整环境（含 blob）集成验证 |

## 7. 下一步

1. **用户环境集成（阻塞完整构建）**：
   - mgk 规则把 `xaga` 加入项目 DTBO 列表
   - 合入 vendor/mediatek 及 alps sibling 项目
   - sensor 追加（§6.3）
2. **真机进系统验证**（模块加载链已通，2026-08-11）：
   - 正常重启（清除 recovery 标志）→ 确认 boot mode 0
   - init 第二阶段 / system 挂载 / zygote 启动
   - 充电流程（顺序见 BRINGUP.md §3.4）：`/sys/class/power_supply/` → 5V 普通充电 → PD 快充
3. **剩余工作（非阻塞）**：指纹（可选）、lm3644 清理（可选）、触控 fw 放置。

## 8. 决策记录要点

| 决策 | 理由 |
|---|---|
| 基于 OPPO `android_kernel_oddo_mt6895` 而非小米 6.6 | 同版本（6.12）优先；小米 6.6 是 GKI common，无 MTK 设备层 |
| qc_cp_manager 不启用 | xaga 是 MTK PD 快充设备；5.10 vendor 配置从未设置 XM_QC_MANAGER |
| 触摸用 NVT36672C 而非 6.12 NT36532 | xaga 实际出货驱动（双击唤醒 + 游戏参数） |
| C7 不移植 fpsgo_cus/msync2_frd_cus | fpsgo 被 6.12 fpsgo_v3 完整覆盖；msync2 核心是闭源 5.10 二进制 |
| 保留 of_gpio.h/of_get_named_gpio | 63 个树内用户；移植更多 5.10 驱动时勿"现代化"成 gpiod |
| 日志捕获双通道（2026-08-13 定案）：**挂死定位用 xaga-marker（XAGR 环）+ 崩溃日志用 oops 分区 kmsg_dumper** | 内核挂死时 kmsg_dumper 不触发（挂死无 oops/panic），用 marker 环（log_store 0x7ffbf000，环最后一条 = 挂死模块，WDT 复位 DRAM 保留）；崩溃（oops/panic）时用 `xaga-dumpregs` kmsg_dumper 把完整 dmesg 写 sdc81（不依赖 LK 恢复）。两者互补（2026-08-09 定案 marker；2026-08-13 增 oops 分区通道） |
| 模块依赖恢复以 alps `BUILD.bazel` 为唯一依据（2026-08-10） | 移植树 Makefile 大量 obj- 被注释（OPPO 走 bazel）；逐符号按 alps srcs/ko_deps/copts 恢复，源码与 alps 逐字节一致，仅 3 处代码级改动（panel_dead 移植 / nt36xxx 桩 / cmdq kvm 守卫） |
| 打包阶段 `llvm-strip --strip-debug`（2026-08-10） | 官方 5.10 ko 无 DWARF（198 ko 共 39.4MB），移植树带 DWARF5（130MB）；strip 保留 .symtab/__ksymtab/modinfo → 25MB，vendor_boot 回到 64MB 规格 |
| 关 `CONFIG_MTK_ECCCI_DRIVER`（2026-08-10） | eccci m 模式 Makefile 结构不完整（alps 走 bazel）；mtk_bp_thl 的 ccci 引用有 IS_ENABLED 守卫，关闭即剔除 |
| `mtk_disp_recovery.c` 换 alps 原版（2026-08-10） | 移植树该文件是 OPPO 修改版，含 oplus_ofp_* 私有调用（6.12 无提供者）；xaga 不需要 OPPO 代码，换原版 + 5.10 移植 panel_dead 接口 |

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
| — | 06ee319 | 恢复 mrdump 模块（aee_sram_printk）+ modules.load 拓扑序（2026-08-10） |
| — | 551d6cd | 恢复 clk-common 模块（get_all_provider_clks，2026-08-10） |
| — | 17f5d4c | **恢复 MTK DRM 模块依赖链（197 ko、0 undefined）+ xaga-drm-restore.md**（2026-08-10） |
| — | 32c3adf | **去 4 对重复导出符号模块**（devapc legacy / mmdvfs-v5 / mmdebug-vcp / gud ffa；197→193 OOT）（2026-08-11，expdb5 实证） |
| — | c5113ed | **vendor_boot 打包 4 个 in-tree 依赖模块**（drm_display_helper/drm_dma_helper/iio buffer/kfifo；193 OOT + 4 = 197）（2026-08-11，expdb 实证） |
| — | 99a5feb | **修复 mediatek-drm-gateic 复合模块**（改名避开 Kbuild 循环依赖）（2026-08-11） |
| — | 84e1aaf | **mmqos DTS 属性名对齐 6.12**（larbs-supply/commons-supply/mmqos-state）（2026-08-11，NULL deref Oops） |
| — | dc99de2 | **build.sh 进度可视化**（时间戳/步骤计数/实时进度条）（2026-08-11） |
| — | f8313fa | **dramc getters NULL drvdata 守卫**（4 个导出函数）（2026-08-11，mmqos Oops） |
| — | 3b9b285 | **禁用 mmc1**（xaga 无 SD 槽，QoS plist_del BUG）（2026-08-11） |
| — | a05b916 | **ufshci 补 mediatek,ufs-disable-mcq**（UFS legacy doorbell）（2026-08-11，init 分区超时） |
| — | 4ac9c34 | **DTS 对齐 alps 6.12**（smi-supply 49 处 / dispsys-num / fifo-size / mmdvfs 前缀 / panel1/2 交换 / xaga.config 增 XAGA_DUMPREGS+DEVTMPFS+USB_G_SERIAL）（2026-08-13，真机 38/42 组件 bound） |
| — | 81093dd | **构建打包：200 OOT ko + mediatek-drm-panel-drv/nvmem/wdt 接线** + modules.dep 依赖序 + llvm-strip（2026-08-13） |
| — | 30c80ea | **修复 disp_pwm0/disp_spr0 compatible 对齐 mtk_ddp_comp_dt_ids**（显示链路根因：crtc0 未创建 → display size 0x0 → atomic oops）（2026-08-13，recovery 可进） |

## 10. expdb 真机诊断史（2026-08-08 ~ 08-11，7 轮）

> 诊断方法：真机刷入构建 → WDT 复位/panic 后 dump LK expdb 分区（`xaga/expdb`，128MB raw，PL_LOG_STORE 机制自动把内核日志写进保留区并随 expdb 恢复）→ 提取文本定位崩溃。**每轮修掉一个 init 阶段问题**，最终 **197 模块全部加载成功**。

| 轮次 | expdb | 失败模式 | 根因 | 修复 commit |
|---|---|---|---|---|
| 1-4 | expdb1-4 | `Unknown symbol` → `Attempted to kill init`（模块加载缺提供者） | devapc/clk-common/mrdump/aee_aed 等 Makefile obj- 被注释（OPPO 走 bazel） | 06ee319 / 551d6cd / 17f5d4c（2026-08-10） |
| 5 | expdb5 | `exports duplicate symbol` → init kill | devapc legacy/common 双模块重复导出 `register_devapc_vio_callback` | 32c3adf |
| 6 | （续 expdb5） | `exports duplicate symbol`（mmdvfs-v5） | mmdvfs-v3/v5 重复导出；alps 平台映射 mt6895 只用 v3 | 32c3adf（+mmdebug/gud 三对一并清理） |
| — | （构建） | — | OOT 依赖 K 树 `=m` 模块（drm_display_helper 等）未打包 | c5113ed |
| 7 | expdb（14:22） | `Unknown symbol mtk_drm_gateic_register` | 复合模块 `-y` 漏主文件 → Kbuild 循环依赖 | 99a5feb |
| 8 | expdb（14:22） | mmqos NULL deref Oops | DTS 属性名 5.10 旧语法（larbs/commons 无 -supply） | 84e1aaf |
| 9 | expdb（14:22） | mmqos→dramc NULL drvdata | dramc probe 缺属性早退，drvdata 未设；getter 无守卫 | f8313fa |
| 10 | expdb（14:46） | msdc1 QoS `plist_del` BUG | xaga 无 SD 槽但 mmc1 被 enable（6.12 mtk-mmc 有 QoS 调用，5.10 无） | 3b9b285 |
| 11 | expdb（15:03） | UFS `legacy doorbell mode not supported` → init 分区超时 | ufshci 缺 `mediatek,ufs-disable-mcq`（5.10 移植丢失） | a05b916 |
| 12 | expdb（15:03 后续） | `libbacktrace.so not found` → init kill | **LK boot mode 2（recovery）**，非内核缺陷；197 ko 已全部加载成功（707ms） | （用户操作：正常重启） |

### 8-12/8-13 显示链路轮次（sdc81 oops 分区 + XAGR 双通道抓日志）

| 轮次 | 现象 | 根因 | 修复 |
|---|---|---|---|
| 13 | recovery 灰屏 / 组件不 bind | fw_devlink 阻塞（cmdq-config 缺失 → gce 永不 probe → dma_configure -517 → mdp_iommu -22） | DTS 补 cmdq-config / smi-supply 49 处 / 16 disp 节点属性（2026-08-12，38→42 组件 bound） |
| 14 | atomic oops（`mtk_dsi_connector_duplicate_state` NULL deref） | display size 0x0：crtc0 未创建 | **探针定位：`Not creating crtc 0 because component 54` → PWM0 compatible `-pwm` 不匹配匹配表 `-pwm0`**（2026-08-13） |
| 15 | （承接） | crtc0 用 ext path（DP_INTF0）→ GET_TIMING 发错 | **30c80ea：disp_pwm0 `-pwm`→`-pwm0` + disp_spr0 `-SPR`→`-spr`** → **recovery 正常进入且显示正常** |

**日志抓取（2026-08-13 定型）**：XAGR 环（log_store → expdb）+ **oops 分区 kmsg_dumper**（`xaga-dumpregs`，崩溃时把完整 dmesg 写 `/dev/block/sdc81`，XGAD 头 + 512KiB；写入链修复：4KB 对齐 bio / IRQ 窗口 / kzalloc 缓冲）。

**诊断要点**：expdb 抓取的内核日志从 setup_arch 头开始（XAGR ring armed），模块加载序列 + panic 尾部完整可见；`grep -aE "Kernel panic|Unable to handle|exports duplicate|Unknown symbol"` 即可定位每轮失败点。

## 11. 相关文档索引

| 文档 | 角色 | 何时看 |
|---|---|---|
| **STATUS.md（本文件）** | 状态总览 | 问"移植到什么程度/还差什么" |
| BRINGUP.md | bring-up 指南 | 构建配方、上电顺序、充电对齐、sensor 合入步骤 |
| README.md | 仓库概览 | 快速了解树结构、特性、提交要点 |
| **xaga-drm-restore.md** | DRM 依赖恢复专项 | 模块依赖关系表（消费者→提供者）+ 依赖分析/移植方法论 + 镜像体积处理（2026-08-10） |
| xaga-log-capture.md | 日志捕获方法 | XAGR 环 / LK expdb / 挂死定位（2026-08-10） |
