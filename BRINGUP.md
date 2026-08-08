# xaga 6.12 Bring-up Guide (以开机为目标)

> Redmi Note 11T Pro / POCO X4 GT / Redmi K50i (xaga, MT6895 / Dimensity 8100)
> 6.12 MTK kernel_device_modules 移植树。目标:编译出可开机的内核 + DTBO + 模块,
> 在真机上完成从"亮屏进系统"到"充电功能"的 bring-up。
> 状态基准:commit 270467e（2026-08-07，工作树干净）。历史经 rebase，旧 hash 见 STATUS.md §9。

---

## 0. 树结构速览

```
xaga/kernel_xiaomi_mt6895-6.12/            # 本移植树(git 仓库, 24 commits)
└── kernel_device_modules-6.12/            # MTK 6.12 OOT 模块源(基于 OPPO oddo6_12, OPPO 专属已剥离)
    ├── arch/arm64/boot/dts/mediatek/      # xaga.dts / xaga_global.dts 板级 overlay
    ├── arch/arm64/configs/vendor/xaga.config   # xaga defconfig 片段
    ├── drivers/...                        # 移植的小米驱动(触摸/充电/面板/背光/触觉)
    └── kernel/kleaf/mgk_64.bzl            # mgk 模块注册表
```

**本树不含**:GKI common 内核(需 OPPO `oddo6_12/android_kernel_oppo_mt6896` 6.12.23 或小米 6.6 对应内核)、`kernel/build`(MTK mgk bazel 规则)、clang 工具链、`vendor/mediatek/kernel_modules`(connectivity/gpu 等 MTK 通用模块, 6.12 版本在 OPPO 树 `vendor/mediatek/kernel_modules/`)。这些来自用户的完整 MTK repo manifest 环境。

---

## 1. 构建(在用户完整 MTK 环境)

### 1.1 需要的组件
| 组件 | 来源 | 说明 |
|---|---|---|
| GKI common 内核 6.12 | OPPO `android_kernel_oppo_mt6896`(6.12.23) | 与 modules 树配对编译 |
| kernel_device_modules-6.12 | 本移植树 | 含 xaga 板级+驱动 |
| kernel/build (mgk 规则) | MTK alps manifest | bazel/kleaf 构建驱动 |
| vendor/mediatek/kernel_modules | OPPO 树或 MTK manifest | connectivity/gpu/met_drv/udc(6.12 版, mt6895 已支持) |
| clang 预编译 | AOSP | - |

### 1.2 项目注册(mgk 侧, 本树无法完成)
- **DTBO 列表**:`kernel/build/bazel_mgk_rules` 中把 `xaga` 加入 project 的 DTBO 列表
  (参考 OPPO 自己的 `arch/arm64/boot/dts/oplus/oplus6895_23021.dts` 注册方式)。
  ⚠️ 本树 `arch/arm64/boot/dts/mediatek/Makefile` **未注册 xaga**(0 处引用);经典构建
  路径需自行添加 `dtb-$(CONFIG_ARCH_MEDIATEK) += xaga.dtb xaga_global.dtb`,
  kleaf 路径则走 mgk 规则的 DTBO 列表(上方主路径)。
- **模块列表**:`mgk_64.bzl` 已注册 xaga 相关模块(见 §2.3);其余模块走
  OPPO/MTK 默认列表,需确认 xaga 不需要的模块(oplus_* 已剥离)。

### 1.3 defconfig
把 `arch/arm64/configs/vendor/xaga.config` merge 到 `mgk_64_k612_defconfig` 之上:
```
scripts/kconfig/merge_config.sh -m -r \
  arch/arm64/configs/mgk_64_k612_defconfig \
  arch/arm64/configs/vendor/xaga.config
```
关键符号:`CONFIG_TOUCHSCREEN_NVT36672C_HOSTDL_SPI=m`(触摸)、
`CONFIG_DRM_PANEL_L16_*`(面板)、`CONFIG_XMEXT_LN8000/SC8551A_CHG_PUMP=m`(充电泵)、
`CONFIG_XMEXT_TI_GAUGE=m`(bq28z610 → "bms" psy)、`CONFIG_XM_PD_MANAGER=m`(充电算法,
**开机后对齐 usb_psy 前保持回退充电, 不插快充头**)、`CONFIG_SIMTRAY_STATUS=m`、
`CONFIG_INPUT_AW8697_HAPTIC=m`、`CONFIG_TOUCHSCREEN_XIAOMI_DOUBLE_CLICK=m`、
`CONFIG_MTK_VIDEO_KTD2687=m`(闪光灯)、`CONFIG_DRM_PANEL_LEDS_KTZ8863A=m`(背光)。

> ⚠️ **oops 分区日志(xaga_oops_log 模块, raw 分区直写)**:`CONFIG_XAGA_OOPS_LOG=m`
> 注册 kmsg dumper,崩溃时用 **panic 安全的轮询 bio** 把 dmesg 文本直接写入
> `/dev/block/sdc81`(offset 0 覆写:64B "XAGA" 头 + 文本 payload)。无需 cmdline、
> 无需预格式化、无需 best_effort。读取:`dd if=/dev/block/sdc81 bs=64 skip=1`
> 或 `strings /dev/block/sdc81`。不使用官方 pstore/blk(best_effort 模式下
> panic 无法落盘,且不注入 best_effort=1 时后端根本不注册)。

### 1.4 产物
`Image.gz` + `mt6895.dtb` + `xaga.dtbo`(+`xaga_global.dtbo`) + 模块 ko 集。
打包进 boot/vendor_boot 时注意:dtbo 用 `xaga.dtbo`(CN 版);开机先用 CN 版。

### 1.5 模块覆盖(官方 198 → 6.12, 无硬缺口)
官方 5.10 ramdisk 的 198 个 .ko(vermagic `5.10.198`, 6.12 内核无法加载)由 6.12 侧四层覆盖:
① 123 打包模块中 109 个同名直接替代;② 更名/合并(clkchk/mt6375-gauge/mtk_system_heap/
pinctrl-mtk-common-v2/fan53870-ldo 等, 详见 STATUS.md §4);③ 约 30 个 6.12 内核内置
(mediatek-drm*/mtk-mmc-autok/regmap-spmi/mac80211 等, 无需 .ko);④ 调试诊断类非必需省略。
**唯一无对应物 = `mi-memory`**(小米私有, 非启动必需)。注意 123 只是打包子集:官方 198 中
未打包的模块在移植树(alps 同步)有源码, 用户环境按 mgk_64.bzl 全量构建(896 目标)可覆盖。

---

## 2. 开机 bring-up(上电顺序)

> 原则:先让内核起来、亮屏、进系统;再逐项开功能。每一步看对应日志特征。

### 2.0 启动链路检查
1. **bootloader 加载**:确认 preloader/lk 把 `mt6895.dtb`(SoC base)+ `xaga.dtbo`(board overlay)按序应用。
   报错特征:`overlay not applied` → DTBO 列表未含 xaga(§1.2)。
2. **early log 检查点**(dmesg):
   - `mt6895` SoC 初始化、`androidboot.hardware=mt6895`(xaga.dts bootargs_ext 已带)
   - DTS 校验:内核会打 `OF: fdt: ...` / 卡死则查 §2.1

### 2.1 若启动卡死/panic,按序排查
| 症状 | 排查 | 对应代码 |
|---|---|---|
| dtc 编译错/加载错 | 板级 DTS 引用未解析(本树已静态验证 210 引用, 再核对你环境里 base 是否一致) | arch/arm64/boot/dts/mediatek/xaga*.dts |
| mtee/svp 相关 panic | xaga.dts 的 memory_ssmr svp-region 依赖 TEE;若无 TEE 固件, 改用 xaga_global.dts(无 svp) | xaga.dts vs xaga_global.dts |
| 卡在 display probe | L16 面板驱动依赖 3 个 provider(见 §3.3), 缺则 probe 失败→黑屏; 先用 `xaga_global.dts` + 确认 `CONFIG_DRM_PANEL_L16_*`+`CONFIG_DRM_PANEL_LEDS_KTZ8863A` 已启 | panel-l16-*.c, leds-ktz8863a.c |
| 卡在触摸 | NVT36672C probe;确认 `CONFIG_TOUCHSCREEN_NVT36672C_HOSTDL_SPI=m`、spi2 节点 | drivers/input/touchscreen/NVT36672C/ |
| 充电 IC probe 失败 | i2c9/i2c7 节点(lm8000/sc8551/bq28z610)已在 DTS;bq28z610 probe 失败→无 "bms" psy(§3.2 依赖它) | drivers/power/supply/{ln8000,sc8551,bq28z610}*.c |

### 2.2 开机必须 probe 成功的驱动(按顺序)
```
mtk 框架(mtk_charger/mtk_battery/mtk_disp_*)  ← 6.12 原生, 一般 OK
├─ bq28z610 (i2c7, 注册 "bms" psy)           ← 电量计, 管理器依赖
├─ ln8000/sc8551 (i2c9, 双充电泵)            ← 快充硬件
├─ NVT36672C (spi2, 触摸)                    ← 必须, 否则无法解锁/进桌面
├─ L16 面板 (dsi0) + ktz8863a 背光 (i2c6)    ← 必须, 否则黑屏
├─ aw8697_haptic (i2c1)                      ← 可选(震动)
└─ simtray (GPIO42)                          ← 可选(卡托检测)
```

### 2.3 模块注册核对(mgk_64.bzl 已有, 构建时确认在列)
`bq28z610` / `sc8551` / `sc8561` / `ln8000_charger` / `pmic_voter` /
`pd_cp_manager` / `charger_class`(power/supply);
`nt36672c`(touchscreen/NVT36672C);`panel-l16-*` + `leds-ktz8863a`(drm/panel);
`aw8697_haptic`(input/misc, 经 ddk_makefile glob);`simtray`(misc, Kconfig 接线)。
> 注:KTD2687 闪光灯走 `drivers/misc/mediatek` 的 BUILD.bazel(mtk-composite 接线, 非
> mgk_64.bzl device_modules);6 颗 sensor 走 src-v4l2(见 §6)。qc_cp_manager 源码在树内但
> 不启用(xaga 是 MTK PD 快充设备, 5.10 也未启用 QC 管理器)。

---

## 3. 开机后的功能对齐(重点: usb_psy / 充电管理器)

> 背景:5.10 xaga 充电 psy 名为 `"usb"`;6.12 树注册为 `"mtk-master-charger"`
> (mtk_charger.c:4127)。drvdata 都是 `struct mtk_charger*`(类型一致, 无需处理)。
> 移植的 usb_get/set_property 与管理器仍查 `"usb"` → 返回 -ENODEV, 字段全 0。

### 3.1 第 1 步: psy 名称对齐 ✅ 已实现
5 处 `power_supply_get_by_name("usb")` → `"mtk-master-charger"`:
```
drivers/power/supply/mtk_charger.c          :4439/4453  (usb_get/set_property 内)
drivers/power/supply/pd_cp_manager.c        :270
drivers/power/supply/pd_single_cp_manager.c :230   (xaga 不构建, 顺带改)
drivers/power/supply/qc_cp_manager.c        :238
```
⚠️ 不要反向把 psy 改名为 "usb"(6.12 内部 20+ 处调用 + 属性表, 风险大)。

### 3.2 第 2 步: USB_PROP 字段写入方 ✅ 已实现(带 CONFIG_XM_PD_MANAGER 保护)
6.12 原无写入方 → 字段恒 0 → 管理器拿不到 typec 方向/PD 状态 → 不进入快充。
已镜像 5.10 调用:
```
drivers/power/supply/mtk_chg_type_det.c  (TCP_NOTIFY_TYPEC_STATE 分支, ~:196)
    usb_set_property(USB_PROP_TYPEC_MODE, POWER_SUPPLY_TYPEC_SINK/AUDIO_ADAPTER/NONE);
    usb_set_property(USB_PROP_TYPEC_CC_ORIENTATION, noti->typec_state.polarity);
drivers/power/supply/mtk_pd_adapter.c    (pd_authentication 成功路径, ~:568)
    usb_set_property(USB_PROP_PD_VERIFYING, 1);
    usb_set_property(USB_PROP_PD_VERIFY_DONE, 0);
    usb_set_property(USB_PROP_APDO_MAX, data->pdp);
    usb_set_property(USB_PROP_PD_AUTHENTICATION, 1);
    (+ #include "mtk_charger.h" under CONFIG_XM_PD_MANAGER)
```

> ✅ **状态(2026-08-06)**:§3.1 的 5 处名称替换(commit 109b0d0)、§3.2 的两个写入方
> (commit 109b0d0/19311e8)均已实现并提交,上机只需验证,无需再改代码。

### 3.3 已知依赖(已移植, 勿删)
- 面板 `panel-l16-*.c` 引用 3 个 provider:`is_tp_doubleclick_enable()`
  (double_click.c)、`get_panel_dead_flag()`(mtk_disp_recovery.c)、`ktz8863a_*`
  (leds-ktz8863a.c) — 均已移植并接线。
- `pd_cp_manager` 还依赖 psy:`"bms"`(bq28z610 注册)、`"battery"`(mtk-battery-manager 注册)。

### 3.4 上机验证顺序
```
1. dmesg 确认上述驱动 probe 成功(§2.2 顺序)
2. ls /sys/class/power_supply/  → 应有 mtk-master-charger, bms, battery
3. zcat /proc/config.gz | grep XM_PD_MANAGER → =m
4. 插 5V 充电器 → 观察 mtk_charger 日志(普通充电, 不依赖管理器)
5. 插 PD 快充头 → pd_cp_manager 日志(依赖 §3.1+§3.2 完成)
6. 未完成 §3.1 前: 管理器会因 usb_psy 失败回退, 不会烧硬件, 放心测试
```

---

## 4. 决策记录(为什么这样做)

| 决策 | 理由 |
|---|---|
| 基于 OPPO oddo6_12 而非小米 6.6 | 同版本(6.12)优先; 小米 6.6 是 GKI common 且无 MTK 设备层 |
| 板级 DTS 直接移植 5.10 ESK 链, 不 include k6895v1_64.dts | 两树定义相同 label → DTC 重复标签错误 |
| 触摸用 NVT36672C 而非 6.12 NT36532 | xaga 实际出货驱动(双击唤醒+游戏参数); NT36532 只有基础绑定 |
| 充电管理器默认开启, 但 usb_psy 对齐留到真机 | 编译无碍; 真机回退安全 |
| C7 不移植 fpsgo_cus/msync2_frd_cus | fpsgo 被 6.12 fpsgo_v3 完整覆盖; msync2 核心是闭源 5.10 二进制 |
| dtbo.dts.0(实机反编译)与移植 DTS 逐节点吻合 | 板级 DTS 得到实机验证, 无需改动 |

## 5. 遗留事项(非开机阻塞)
- [x] §3.1 psy 名称对齐 — **已实现并提交**(commit 109b0d0)
- [x] §3.2 USB_PROP 写入方(mtk_chg_type_det + mtk_pd_adapter)— **已实现并提交**(commit 109b0d0/19311e8)
- [ ] 真机上验证 §3.4 验证顺序(需完整构建环境 + 设备)
- [ ] 触控 fw(nt36672e fw 文件)放入 vendor 分区对应路径
- [ ] xaga_global 变体开机验证(若 CN 版 TEE/svp 有问题时用)
- [ ] mtk-master-charger 名字的 kABI/模块加载顺序核对(若有 modprobe 依赖)
- [ ] **指纹驱动(goodix_cap)**: DTS 有 `goodix,goodix-fp` 节点(cust_mt6895_fingerprint.dtsi), 5.10 用 drivers/input/fingerprint/goodix_cap/(GF3626ZS9 TEE), 6.12 未移植 —— 依赖 5.10 内核私有 mtk_spi.h(用户环境), 移植步骤见 README.md 已知缺口 §1
- [ ] **sensor 用户环境合入**: 在用户环境 `src-v4l2/BUILD.bazel` 的 `config_cust_kernel_imgsensor` 追加 6 个 xaga* 名字(步骤见 §6)
- [ ] **lm3644 注册残留清理**(可选): `mgk_64.bzl:1361` 给 mt6895 注册了 lm3644, xaga 用 KTD2687 不用, 保留无害可删

## 6. 相机 sensor 移植(2026-08-07, 已提交 7f75af8 + f3e8e80)
xaga 的 6 颗 camera sensor 驱动已从 5.10 ESK 移植到本树:

| sensor | 角色 | 目录 |
|---|---|---|
| s5khm2 | 主摄(108MP) | `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xagas5khm2_mipi_raw/` |
| s5k4h7 | 主摄备份 | `.../xagas5k4h7_mipi_raw/` |
| ov16a1 | 前摄 | `.../xagaov16a1_mipi_raw/` |
| s5kgw1 | 超广角 | `.../xagas5kgw1_mipi_raw/` |
| gc02m1 | 微距 | `.../xagagc02m1_mipi_raw/` |
| ov02b10 | 前摄备份 | `.../xagaov02b10_mipi_raw/` |

已做适配:
- `subdrv.mk` → 6.12 `Makefile`(`imgsensor-objs += $(subdrv-rpath)/<name>mipiraw_Sensor.o`)
- `kd_imgsensor.h`: 补 6 个 `XAGA*_SENSOR_ID` + `SENSOR_DRVNAME_XAGA*` 宏(从 5.10 原样搬入)
- gc02m1/ov02b10(8-bit reg): `subdrv_i2c_{rd,wr}_u8_u8` → 6.12 `_u8_reg8`; 缺 `wr_regs_u8_u8`(2 字节表写), 在 sensor 内新增本地 `*_table_write()` 循环实现
- 6 颗 sensor 均通过 -fsyntax-only 对 6.12 mtkcam v4l2 框架(subdrv_ctx/subdrv_ops/subdrv_entry)的编译验证

**用户完整 MTK 环境合入步骤(bazel/kleaf 路径, 本树无法完成)**:
1. 把本树 `vendor/mediatek/kernel_modules/mtkcam/imgsensor/src-v4l2/common/xaga*/` 6 个目录合入用户环境同路径(或直接使用本树 vendor/ 增量)
2. 在用户环境 `mtkcam/imgsensor/src-v4l2/BUILD.bazel` 的 `config_cust_kernel_imgsensor` 字符串中追加:
   `xagas5khm2_mipi_raw xagas5k4h7_mipi_raw xagaov16a1_mipi_raw xagas5kgw1_mipi_raw xagagc02m1_mipi_raw xagaov02b10_mipi_raw`
   (bazel 路径按该硬编码列表 ∩ common/**/Makefile 收集; make/Kbuild 路径则自动读 CONFIG_CUSTOM_KERNEL_IMGSENSOR, xaga.config 已声明, 无需改)
3. xaga.config 的 `CONFIG_CUSTOM_KERNEL_IMGSENSOR` 已含 6 颗(不必动)
4. DTS 侧 `xaga_mt6895_camera_v4l2.dtsi` 的 imgsensor 节点 compatible 对应 sensor 名, 已由板级 DTS 提供

注意: sensor 驱动只依赖 v4l2 框架(用户环境 mtkcam), 不在 mgk_64.bzl 的 device_modules 列表; 若构建报缺 `subdrv_i2c_wr_u8_u8` 之类符号, 说明用户环境 mtkcam 版本更老, 以本树 Makefile/本地表写为准即可。

## 7. 闪光灯与快充协议(2026-08-07, 非开机阻塞)

### 7.1 KTD2687 相机闪光灯(commit e321232/1aadba1)
- 驱动:`drivers/misc/mediatek/flashlight/v4l2/ktd2687.c`,双灯(v4l2 subdev)。
- 接线:`CONFIG_MTK_VIDEO_KTD2687=m` + flashlight 核心/composite 已恢复(顶层 Kbuild
  obj-y + `drivers/misc/mediatek` BUILD.bazel ddk_makefile/ddk_kconfigs)。
- 上机验证:`/sys/class/leds/` 出现 ktd2687 相关节点,相机开闪光灯日志无 probe 失败。

### 7.2 MTK Pump Express 协议(commit 4b2d268)
- 恢复 pep/pep20/pep40/pep45/pep50/pep50p 六个协议模块(`mtk_pep*`, 自 alps 同步),
  注册进 mgk_64.bzl device_modules。
- 配合 `CONFIG_XM_PD_MANAGER=m` 在 PD 快充协商时选择 PE 协议分支, 不阻塞开机。
