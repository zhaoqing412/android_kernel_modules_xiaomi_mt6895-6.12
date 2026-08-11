# xaga 6.12 MTK DRM 模块恢复总结（2026-08-10）

## 背景

xaga 6.12 移植树（`kernel_device_modules-6.12`）从 OPPO 6.12 拷贝而来。OPPO 侧用
bazel/kleaf（`BUILD.bazel` + `kernel/kleaf/mgk_64.bzl`）构建全部 DRM 模块，移植树改用
Kbuild Makefile 构建后，**大量 DRM 相关 Makefile 的 `obj-`/`ccflags` 行仍是注释状态**，
导致依赖这些模块的驱动（panel / 触控 / 背光 / DP 选择器）在 init 首阶段加载时
`Unknown symbol` 失败 → `Attempted to kill init` panic。

本总结记录 2026-08-10 恢复 MTK DRM 模块全链路的改动，全部以 alps
（`xaga/baselines/modules/alps-kernel_device_modules-6.12`）的 `BUILD.bazel` 为唯一依据。

---

## 〇、依赖分析重点（移植者必读）

本节的目的是让后续移植者**系统性排查**同类"模块间 Unknown symbol"问题，
而不是等真机 panic 后逐符号补。核心结论：

> **所有缺符号的根因几乎都不是源码缺失，而是"提供者模块没被 Makefile 编译"。
> 移植树从 OPPO 拷贝时，大量 Makefile 的 obj- 行被注释（OPPO 走 bazel），
> 只要按 alps 的 `BUILD.bazel` 恢复编译，符号就自动闭环。**

### 1. 发现缺符号的正确方法（不用等真机）

真机 expdb 一次只暴露**第一个**加载失败的符号（init 串行加载 modules.load，
任一失败即 panic）。要一次性看全所有缺口：

```bash
# 收集全部导出符号（K 树 vmlinux + 模块树）
cat $OUT/Module.symvers $M/Module.symvers | awk '{print $2}' | sort -u > /tmp/exp.txt

# 对每个 .ko 求 undefined 符号，与导出集求差集 = 真实缺口
find $M -name "*.ko" | while read f; do
  $HOME/clang/bin/llvm-nm --undefined-only "$f" 2>/dev/null \
    | awk '{print $NF}' | grep -v '^_GLOBAL_OFFSET_TABLE_' | sort -u > /tmp/u.txt
  MISSING=$(comm -23 /tmp/u.txt /tmp/exp.txt | tr '\n' ' ')
  [ -n "$MISSING" ] && echo "$(basename $f) :: $MISSING"
done
```

⚠️ `llvm-nm` 输出注意：有地址时类型是 `$2`，无地址（如 U 符号）时类型是 `$1`，
**一律用 `$NF` 取符号名**。

### 2. 每个缺符号的定位流程

```bash
# ① 在 alps 找 EXPORT_SYMBOL 提供者（唯一权威依据）
grep -rln "EXPORT_SYMBOL.*$sym\b" $ALPS/ --include="*.c" | head -1
#    → 得到提供者 .c 文件

# ② 找该 .c 属于 alps 哪个模块（BUILD.bazel 的 srcs 清单）
# ③ 检查移植树对应目录 Makefile：obj 行是否被注释 / 父级是否下降
# ④ 对照 alps bazel 的 srcs/ko_deps/copts 恢复
```

### 3. 三类可执行性判断（不是所有缺口都要编译）

| 情况 | 判定 | 处理 |
|---|---|---|
| 提供者 .c 在移植树，Makefile 注释 | 恢复编译（最常见） | 按 alps bazel srcs 取消注释 obj |
| 提供者在 K 树 vmlinux（System.map 有 T 符号） | 无需处理 | — |
| 符号是 OPPO/小米私有（两树 + K 树都无 EXPORT） | 不可编译 | 换 alps 原版文件 / 桩化 / 关 CONFIG / 从 modules.load 过滤 |

判断私有的快速法：`grep -rln "EXPORT_SYMBOL.*$sym"` 在 **alps、移植树、K 树三个
root** 都搜一遍，都无则必是私有。

### 4. 关键参考：官方 5.10 modules.load（判断"必需性"的锚点）

解包官方 vendor_boot 的 ramdisk 拿 `modules.load`，官方没有的模块（如
`mtk_peak_power_budget` / `mtk_bp_thl` / `hwccf` / `mtk_drm_gateic`）说明
**官方 5.10 是内核内置或根本不带** —— 6.12 模块化后可以：
- 若符号有 `IS_ENABLED(CONFIG_X)` 守卫 → 关掉对应 CONFIG 消除依赖（如
  `mtk_bp_thl` 的 ccci 引用由 `CONFIG_MTK_ECCCI_DRIVER` 守卫）；
- 否则从 6.12 `modules.load` 移除该模块（但要先确认无其他模块依赖其导出符号）。

### 5. 最隐蔽的三类坑

1. **复合模块 `-y` 含自身**：`obj-m += foo.o` + `foo-y := foo.o bar.o` 会
   `cannot open foo.o` 或 duplicate symbol。Kbuild 惯例是 **`-y` 不含自身**
   （主源同名自动编入），或模块名与主源不同名（如 alps 的 `ufs-mediatek-mod`、
   `iommu_debug`、`mtk_gpufreq_wrapper` 都是模块名 ≠ 源名）。
2. **alps 源码依赖 bazel copts 的 Wno 宽限**：mediatek_v2 等目录必须整段恢复
   alps `copts` 里的 `-Wno-implicit-function-declaration` 等 20+ 项，
   否则 clang 对隐式声明 / int 转指针直接报错（移植树全局 -Werror）。
3. **trace 事件头相对路径**：`#include "xxx_trace.h"` 经 `define_trace.h`
   展开成 `./xxx_trace.h`，必须 `ccflags-y += -I$(src)` 才能找到。

### 6. 依赖恢复顺序（拓扑）

先恢复**被依赖方**（导出者），再恢复依赖方（引用者）。一次全量
`make M= ... modules` 后，用第 1 节的 nm 审计看收敛情况，反复迭代——
**每次编译都先跑全量 nm 审计，不要等真机**。



## 一、mediatek_v2（MTK 私有 V2 DRM 框架，最大项）

### 涉及文件
`drivers/gpu/drm/mediatek/mediatek_v2/Makefile`

### 问题
- 原 Makefile 5–161 行 `mediatek-drm-y` 列表与 `obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)`
  全被注释（OPPO 走 bazel）。
- 编译报错链：缺 `drm_internal.h` / `mmqos-mtk.h` / `hwccf_provider.h` /
  `mtk_layer_layout_trace.h` / `iommu_debug.h` 等头文件，以及 alps 源码依赖的
  `-Wno-implicit-function-declaration` / `-Wno-int-conversion` 等宽限 flag。

### 改动
1. 恢复 `mediatek-drm-y` 完整列表（对齐 alps `BUILD.bazel` 的 `srcs`，80+ 文件）。
2. **补两个 alps 有、移植树 y 列表遗漏的文件**（编译不报错但链接缺符号）：
   - `mtk_disp_bdg.o`（bridge，导出 `bdg_*` / `is_bdg_supported` / `get_ap_data_rate` 等）
   - `mtk_disp_dbgtp.o`（DBGTP，导出 `mtk_dbgtp_*` / `mml_dbgtp_register`）
3. 恢复 alps bazel `copts` 的 Wno 宽限清单（20+ 项），因为 alps 源码本身依赖这些
   默认关闭/隐式声明：
   ```make
   ccflags-y += -Wno-implicit-function-declaration -Wno-int-conversion ...
   ```
4. 补充 -I 路径（对照 alps `copts`）：
   - `-I$(srctree)/drivers/gpu/drm/`（`drm_internal.h` 在 K 树）
   - `-I$(DEVICE_MODULES_PATH)/drivers/interconnect/mediatek/`（`mmqos-mtk.h`）
   - `-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/hwccf/include`（`hwccf_provider.h`）
   - `-I$(src)`（trace 事件 `mtk_layer_layout_trace.h` 相对路径）
   - `-I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2`（自目录头）
5. `mtk_dump.h` 补 `void mtk_dump_dbg_slot(void);` 声明
   （alps 靠 `-Wno-implicit-function-declaration` 压制，移植树 -Werror 需显式声明）。

### 依赖闭环（mediatek-drm.ko 的跨模块符号）
恢复后 `mediatek-drm.ko` 的未定义符号指向以下模块（全部按 alps `ko_deps` 恢复）：
- `mtk_sync` / `mtk_panel_ext` / `mtk_disp_notify` → 见下文 dummy_drm/v2 三模块
- `mml_*` → `mtk-mml.ko`
- `slbc_*` → `mtk_slbc.ko`
- `hwccf_is_enabled` → `hwccf.ko`
- `dma_buf_get_gid` → `system_heap.ko`（drivers/dma-buf/heaps）
- `notify_vb_audio_control` → `mtk-afe-external.ko`（sound/soc/mediatek/common）
- `get/set_panel_dead_flag` → 本树 mtk_disp_recovery.c 内新增导出（见下）

---

## 二、mtk_disp_recovery.c（ESD 恢复 / panel_dead 接口）

### 涉及文件
`drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_recovery.c`

### 问题
- 移植树该文件是 **OPPO 修改版**（1281 行 vs alps 1049 行），含
  `#ifdef OPLUS_FEATURE_DISPLAY` / `oplus_ofp_get_aod_state()` 等 OPPO 私有调用，
  这些符号 6.12 两树均无提供者 → 编译期 undefined + 运行期必崩。
- 5.10 xaga 的 `get_panel_dead_flag` / `set_panel_dead_flag`（`panel-l16-*.c` /
  `nvt36672c.ko` 依赖）在 6.12 alps 中被移除。

### 改动
1. **整文件换回 alps 原版**（`cp alps/.../mtk_disp_recovery.c`，OPPO 私有代码对
   xaga 无意义且符号无提供者）。
2. 文件末尾**从 5.10 移植 panel_dead 接口**（atomic + 双导出），panel/触控依赖：
   ```c
   static atomic_t panel_dead;
   int get_panel_dead_flag(void) { return atomic_read(&panel_dead); }
   EXPORT_SYMBOL(get_panel_dead_flag);
   void set_panel_dead_flag(int value) { atomic_set(&panel_dead, value); }
   EXPORT_SYMBOL(set_panel_dead_flag);
   ```

---

## 三、dummy_drm vs mediatek_v2 的 mtk_panel_ext / mtk_sync / mtk_disp_notify

### 涉及文件
- `drivers/gpu/drm/mediatek/Makefile`（父级分支）
- `drivers/gpu/drm/mediatek/mediatek_v2/Makefile`
- `drivers/gpu/drm/mediatek/dummy_drm/Makefile`

### 问题
- 两目录各有一份 `mtk_panel_ext.c/.h`（guard 不同），dummy_drm 版是
  `CONFIG_DRM_MEDIATEK_DUMMY` 配置专用。
- xaga 配置 `CONFIG_DRM_MEDIATEK_DUMMY` 未开、`CONFIG_DRM_MEDIATEK_V2=y` →
  父 Makefile 走 V2 分支，dummy_drm 不下降。
- alps 中 `mtk_panel_ext` / `mtk_sync` / `mtk_disp_notify` 是 **V2 构建下注册的
  独立 ko**（`mediatek_v2/BUILD.bazel` 三个 `define_mgk_ddk_ko`），源文件在
  `mediatek_v2/` 目录，与 v2 源码逐字节一致（diff 0）。

### 改动
- 在 `mediatek_v2/Makefile` 追加三个独立模块注册：
  ```make
  obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) += mtk_panel_ext.o
  obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) += mtk_sync.o
  obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) += mtk_disp_notify.o
  ```
- 移除父 Makefile V2 分支对 `dummy_drm/` 的额外下降（避免 dummy 版头与 v2 版
  枚举重定义冲突）。
- dummy_drm/Makefile 里多余的 `obj-m +=` 冗余行删除（alps 原版
  `obj-$(CONFIG_DEVICE_MODULES_DRM_MEDIATEK)` 已覆盖）。

---

## 四、mtk-mml（MML 模块）

### 涉及文件
`drivers/gpu/drm/mediatek/mml/Makefile`

### 问题
- 原 Makefile 的 `ccflags-y`（-I 列表）、`obj-$(CONFIG_MTK_MML)`、
  `mtk-mml-objs` 全注释。
- 脚本批量取消注释时**误伤 `#ifeq` 块**（`CONFIG_MTK_MML_DEBUG` /
  `CONFIG_FPGA_EARLY_PORTING` / `MGK_INTERNAL`），导致 `endif` 失配
  （`Makefile:140: 不该出现的 endif`）。

### 改动
1. 恢复 `ccflags-y`（含 mediatek_v2/dpc/devfreq/heaps/smi/cmdq/slbc/iommu 等 -I）。
2. 恢复 `obj-$(CONFIG_MTK_MML) := mtk-mml.o` + 平台模块（mt6895 等）。
3. 恢复 `mtk-mml-objs` 列表，并**补 `mtk-mml-dbgtp.o`**
   （alps `BUILD.bazel` srcs 有、移植树原注释列表遗漏；导出 `mml_dbgtp_*`）。
4. 修复误取消注释的 `#ifeq` 块（体与 `#endif` 一并恢复注释）。

---

## 五、panel 目录：mtk_drm_gateic

### 涉及文件
`drivers/gpu/drm/panel/Makefile`

### 问题
- `mtk_drm_gateic.c` 是 alps 中的孤儿文件（无构建注册），但 `leds-mtk-disp.c`
  声明 `extern int __weak mtk_drm_gateic_set_backlight(...)` 并可能调用
  （`MTK_COMMON_LCM_DRV` 背光路径）——弱符号无提供者时调用即崩。

### 改动
- 编为复合模块（gateic + 两个 IC 驱动 + I2C 助手）：
  ```make
  obj-m += mediatek-drm-gateic.o
  mediatek-drm-gateic-y := mtk_drm_panel_i2c.o \
      mtk_drm_gateic_rt4801h.o \
      mtk_drm_gateic_rt4831a.o \
      mtk_drm_gateic.o
  ```
- ⚠️ **2026-08-11 真机修正（commit 99a5feb）**：模块名**必须与主源对象不同名**。
  初版写 `obj-m += mtk_drm_gateic.o` + `mtk_drm_gateic-y := ... mtk_drm_gateic.o`
  （含自身）→ Kbuild 循环依赖，主文件**从未编译**，`mtk_drm_gateic_register`/
  `mtk_gateic_match_lcm_list` 变成 U 符号 → 真机 insmod `Unknown symbol` → init kill。
  改名 `mediatek-drm-gateic`（官方 5.10 xagaforge 名 + alps mgk_64.bzl 引用的名字），
  `-y` 列表含主文件 `mtk_drm_gateic.o`（与模块名不同，无循环）。

---

## 六、mediatek_v2 依赖的外围模块（按 alps ko_deps 恢复）

mediatek-drm / mtk-mml / panel 的符号依赖链牵出的外围模块，全部按 alps
`BUILD.bazel` 恢复：

| 模块 | 提供符号 | 恢复方式 |
|---|---|---|
| `mtk-mml.ko` | `mml_drm_*` | mml/Makefile 恢复（见上） |
| `mtk_slbc.ko` | `slbc_request` 等 | slbc/Makefile 恢复 obj + slbc_mt6895 |
| `slbc_trace.ko` | `slbc_trace_rec_write` | slbc/Makefile 恢复 obj |
| `hwccf.ko` | `hwccf_is_enabled` | hwccf/Makefile 恢复 obj + `-I soc/mediatek` |
| `system_heap.ko` | `dma_buf_get_gid` | **新建** drivers/dma-buf/heaps/Makefile |
| `mtk-afe-external.ko` | `notify_vb_audio_control` | sound/soc/mediatek/common 恢复 obj |
| `mtk_gpufreq_wrapper.ko` + `mtk_gpufreq_mt6895.ko` | `gpufreq_set_mfgsys_config` | gpufreq/v2/Makefile 恢复（ccflags + obj） |
| `mtk_gpu_hal.ko` | `mtk_get_gpu_*_fp` | **恢复** hal/Makefile（Makefile.backup 改名）+ 父级下降 |
| `mtk-mmdvfs-v3.ko` | `mtk_mmdvfs_camera_notify` / `genpd_notify` | soc/mediatek/Makefile 恢复 obj |
| `mtk-mmdvfs-v5.ko` | `mtk_mmdvfs_enable_vcp` | soc/mediatek/mmdvfs/Makefile 恢复 obj |
| `mtk-mmdebug-vcp(-stub).ko` | `mmdebug_is_init_done` | mmdebug/Makefile 新建 obj 行 |
| `mtk-vmm-notifier.ko` | `mtk_vmm_ctrl_dbg_use` | vmm/Makefile 恢复 obj |

### 父级下降修复（Kbuild / 顶层 Makefile）
- `Kbuild`：补 `obj-y += drivers/dma-buf/heaps/`
- `drivers/gpu/mediatek/Makefile`：`CONFIG_MTK_GPU_SUPPORT` 块补 `obj-y += hal/`
- 各子目录 Makefile 恢复后即可被父级遍历。

---

## 七、编译踩坑记录（同类问题汇总）

| 症状 | 根因 | 修复 |
|---|---|---|
| `ld.lld: cannot open foo.o` / `duplicate symbol` | 复合模块 `-y` 列表**含自身同名对象**（`mtk_drm_gateic-y := ... mtk_drm_gateic.o`），Kbuild 自我聚合 | `-y` 不含自身；或模块名与主源不同名（`ufs-mediatek-mod`、`iommu_debug` 先例） |
| `missing MODULE_LICENSE()` | 复合模块主对象未编入 | 模块名 ≠ 源名，或 `-y` 正确组织 |
| `fatal error: './xxx_trace.h' file not found` | trace 事件头用 `./` 相对，需要 `-I$(src)` | Makefile 补 `ccflags-y += -I$(src)` |
| `fatal error: 'xxx.h' not found` | 头在 K 树（`$(srctree)`）或兄弟目录 | 对照 alps `copts` 逐条补 -I |
| `error: variable set but not used [-Werror]` | alps 原样代码在移植树 `-Wall -Werror` 下触发 | per-obj `ccflags-y += -Wno-...`（不改源码） |
| `Makefile: NN: 不该出现的 endif` | 脚本批量取消注释误伤 `#ifeq` 块 | 检查 ifneq/ifeq/endif 配对 |
| 隐式函数声明 / int 转指针 | alps 源码依赖 bazel `copts` 的 Wno 宽限 | mediatek_v2 整目录恢复 alps Wno 清单 |

---

## 七·补、完整依赖关系表（消费者 → 提供者）

以下依赖由 2026-08-10 全量 `llvm-nm` 审计得出（所有 `.ko` 的 undefined 符号与
全部导出符号求差集），**恢复后 0 undefined**。供其他移植者对照：

### 7.1 DRM 消费者 → 提供者

| 消费者模块 | 缺失符号 | 提供者模块 | 恢复动作 |
|---|---|---|---|
| `mediatek-drm.ko` | `find_panel_ctx`/`mtk_panel_ext_create`/`mtk_panel_remove`/`mtk_panel_detach`/`find_panel_ext`/`mtk_drm_get_lcm_version` | `mtk_panel_ext.ko` | v2/Makefile 注册 |
| `mediatek-drm.ko` | `mtk_sync_fence_create`/`mtk_sync_timeline_*`/`mtk_sync_share_fence_create` | `mtk_sync.ko` | v2/Makefile 注册 |
| `mediatek-drm.ko` | `mtk_disp_notifier_call_chain`/`mtk_disp_sub_notifier_*`/`mtk_disp_3rd_notifier_*` | `mtk_disp_notify.ko` | v2/Makefile 注册 |
| `mediatek-drm.ko` | `mml_drm_*`（20+ 符号） | `mtk-mml.ko` | mml/Makefile 恢复 |
| `mediatek-drm.ko` | `slbc_*`（request/release/validate/power_on/off/invalidate） | `mtk_slbc.ko` | slbc/Makefile 恢复 |
| `mediatek-drm.ko` | `hwccf_is_enabled` | `hwccf.ko` | hwccf/Makefile 恢复 |
| `mediatek-drm.ko` | `dma_buf_get_gid` | `system_heap.ko` | **新建** heaps/Makefile |
| `mediatek-drm.ko` | `notify_vb_audio_control` | `mtk-afe-external.ko` | sound/common 恢复 |
| `mediatek-drm.ko` | `get/set_panel_dead_flag` | `mediatek-drm.ko` 自身（mtk_disp_recovery.c 新增导出） | 5.10 移植 |
| `mediatek-drm.ko` | `bdg_*`（15 个）/`mtk_spi_*`/`get_ap_data_rate`/`is_bdg_supported`/`check_stopstate` | `mediatek-drm.ko` 自身（mtk_disp_bdg.o） | 补进 y 列表 |
| `mediatek-drm.ko` | `mtk_dbgtp_*`（17 个）/`mml_dbgtp_register` | `mediatek-drm.ko` 自身（mtk_disp_dbgtp.o） | 补进 y 列表 |

### 7.2 面板 / 触控 / 背光 → 提供者

| 消费者模块 | 缺失符号 | 提供者模块 |
|---|---|---|
| `panel-l16-42-02-0a-dsc-vdo.ko` / `panel-l16-36-02-0b-dsc-vdo.ko` | `find_panel_ctx`/`find_panel_ext`/`get_panel_dead_flag`/`mtk_panel_detach`/`mtk_panel_ext_create`/`mtk_panel_remove` | `mtk_panel_ext.ko` + `mediatek-drm.ko`（dead_flag） |
| `nvt36672c.ko` | `get_lockdown_info_for_nvt`/`mtk_disp_notifier_register`/`set_panel_dead_flag` | **桩化**（6.12 无 mi_disp）+ `mtk_disp_notify.ko` + `mediatek-drm.ko` |
| `leds-mtk-disp.ko` | `mtk_drm_gateic_set_backlight`/`mtk_drm_get_conn_obj_id_from_idx`/`mtk_drm_get_lcm_version`/`mtk_drm_set_conn_backlight_level`/`mtkfb_set_backlight_level`/`_gate_ic_backlight_set` | `mtk_drm_gateic.ko`（__weak 提供）+ `mediatek-drm.ko` + `rt4831a_drv.ko` |
| `leds-mtk-pwm.ko` | `mtk_drm_get_conn_obj_id_from_idx` | `mediatek-drm.ko` |
| `usb_dp_selector.ko` | `mtk_dp_aux_swap_enable`/`mtk_dp_set_pin_assign`/`mtk_dp_SWInterruptSet` | `mediatek-drm.ko` |
| `mtk-mml.ko` | `mml_dbgtp_*` | `mtk-mml.ko` 自身（mtk-mml-dbgtp.o） |

### 7.3 非 DRM 但同批修复的外围链

| 消费者模块 | 缺失符号 | 提供者模块 |
|---|---|---|
| `mtk_peak_power_budget.ko` | `gpufreq_set_mfgsys_config` | `mtk_gpufreq_wrapper.ko`（gpufreq/v2） |
| `mtk_peak_power_budget.ko` | `get_gpueb_ipidev`/`mtk_ipi_send_compl_to_gpueb` | `mtk_gpueb.ko` |
| `mtk_peak_power_budget.ko` | `get_mcupm_ipidev`/`get_mcupms_ipidev_number` | `mcupm_v3.ko` |
| `mtk_gpufreq_wrapper.ko` | `mtk_get_gpu_*_fp`（4 个） | `mtk_gpu_hal.ko`（hal/Makefile 恢复） |
| `mtk_bp_thl.ko` | `ccci_set_power_throttle_cb`/`exec_ccci_kern_func` | ~~eccci~~ → **关 CONFIG_MTK_ECCCI_DRIVER**（IS_ENABLED 守卫剔除） |
| `mtk-mmdvfs-v3.ko`（依赖方 vmm） | `mtk_mmdvfs_camera_notify`/`mtk_mmdvfs_genpd_notify` | `mtk-mmdvfs-v3.ko` 自身 |
| `mtk-mmdvfs-v5.ko` | `mtk_vmm_ctrl_dbg_use` | `mtk-vmm-notifier.ko` |
| `mtk-mmdvfs-v5.ko` | `mmdebug_is_init_done` | `mtk-mmdebug-vcp.ko` |
| `mmqos-common.ko` | `mtk_icc_*` | `mtk-icc-core.ko` |
| `mmqos-common.ko` | `mtk_dramc_get_ddr_type` | `mtk_dramc.ko` |
| `mmqos-common.ko` | `mtk_mmmc_*` | `mtk-mm-monitor-controller.ko` |
| `mmqos-common.ko` | `mtk_mmdvfs_enable_vcp` | `mtk-mmdvfs-v5.ko` |
| `mtk-smi.ko` / `mtk-mminfra-debug.ko` | `mtk_mminfra_on_off` | `mtk-mminfra-util-dummy.ko` |
| `mtk-smi-dbg.ko` | `mtk_emidbg_dump` | `emi.ko` |
| `mtk-mmc.ko` | `cqhci_init`/`cqhci_irq`/`cqhci_deactivate` | `cqhci.ko` |
| `mtk-mmc.ko` / `mtk-mmc-dbg.ko` | `mmc_mtk_biolog_*`/`set_mmc_perf_mode` | `blocktag.ko` |
| `rpmb-mtk.ko` | `rpmb_mtk_cmd_req` | `core.ko`（rpmb） |
| `rpmb-mtk.ko` | `mc_*`（7 个 TEE 符号） | `mcDrvModule.ko`（tee/gud/700） |
| `rpmb-mtk.ko` | `ufs_mtk_rpmb_get_raw_dev` | `ufs-mediatek-mod.ko` |
| `mtk_tinysys_ipi.ko` | `mtk_rpmsg_create_channel`/`create_device` | `mtk_rpmsg_mbox.ko` |
| `mtk-mmdvfs.ko`（mtk-mmdvfs-v5） | `mmprofile_*`（5 个） | `mmprofile.ko` |
| `ps5170.ko` | `ssusb_get_drvdata`/`ssusb_power_*_notifier` | `mtu3.ko` |
| `clkchk-mt6895.ko` / `mtk-mminfra-debug.ko` | `register_devapc_*` | `device-apc-common(-legacy).ko` |
| `spmi-mtk-mpu.ko` | `ext_pmif_base` | `spmi-mtk-pmif.ko`（pmif-core.o） |
| `flashlight.ko` | `kicker_ppb_request_power`/`register_bp_thl_notify` | `mtk_peak_power_budget.ko` + `mtk_bp_thl.ko` |
| `mtk_low_battery_throttling.ko` | `dump_lvsys_thd`/`lvsys_*` | `pmic_lvsys_notify.ko` |
| `slbc_ipi.ko` | `slbc_trace_rec_write` | `slbc_trace.ko` |
| `rt5133-regulator.ko` | `devm_extdev_io_device_register` | `extdev_io_class.ko`（subpmic） |

### 7.4 依赖恢复的最终判定

- **197 个 .ko 全部无 undefined 符号**（nm 审计 0 缺口）。
- 3 处无法编译的符号按"可执行性判定"处理：`get_lockdown_info_for_nvt`（桩化）、
  `ccci_*`（关 CONFIG）、OPPO `oplus_ofp_*`（换 alps 原版文件）。
- 模块加载顺序由 build.sh 的 **python Kahn 拓扑排序**（读 depmod modules.dep）保证，
  不靠 modules.load 字母序。

## 八、验证结果

- 全量模块编译：**197 个 .ko，0 个 modpost undefined 符号**
- 与 alps 源码对比：所有恢复模块的 .c/.h 与 alps **逐字节一致**（diff 0），
  仅 3 处代码级改动（panel_dead 移植、nt36xxx 桩、cmdq kvm 守卫，均标注
  `xaga:` 注释）。
- DRM 相关旧缺口（panel-l16 / nvt36672c / leds-mtk-disp / leds-mtk-pwm /
  usb_dp_selector / mtk-mml / mediatek-drm）的 undefined 符号全部闭环。
- **镜像体积（2026-08-10 追加）**：官方 vendor_boot ramdisk ko 合计 39.4MB，
  移植树恢复的 197 ko 带 DWARF5 调试段达 130MB → build.sh 打包阶段加
  `llvm-strip --strip-debug`（保留 .symtab/__ksymtab/modinfo）→ **25MB**，
  vendor_boot_new.img 从 90MB 降回 64MB 规格（与官方一致）。boot_new.img
  结构不变（kernel 13.2MB + ramdisk 1.77MB，64MB pad，与官方一致）。

## 九、2026-08-11 真机修正（模块集定型）

本文件记录的是 2026-08-10 恢复链的 197 ko 快照；2026-08-11 经 7 轮 expdb
真机诊断，打包集最终定型为 **193 OOT + 4 in-tree = 197**（详见 STATUS.md §4a/§10）：

- **裁剪 4 对重复导出符号模块**（真机 `exports duplicate symbol` → init kill；
  两模块导出同名符号，按 alps mgk_64.bzl 平台映射各留其一）：
  `device-apc-common-legacy` / `mtk-mmdvfs-v5`（mt6993 专属）/
  `mtk-mmdebug-vcp` / `mcDrvModule-ffa`。
- **gateic 改名 `mediatek-drm-gateic`**（见 §五）——本节初版接线真机失败。
- **vendor_boot 追加 4 个 K 树 in-tree 模块**（`=m` 不编进 Image，但被 OOT 引用）：
  `drm_display_helper.ko`（drm_dp_*）、`drm_dma_helper.ko`（drm_gem_dma_vm_ops）、
  `industrialio-triggered-buffer.ko` + `kfifo_buf.ko`（mt6375-adc）。
- **真机结果**：init 首阶段 **197 ko 全部加载成功（707ms）**——本文件的依赖链
  恢复 + 上述修正共同使模块加载阶段全通。

## 附：相关文件索引

- `drivers/gpu/drm/mediatek/mediatek_v2/Makefile`（+119 行，主恢复）
- `drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_recovery.c`（-232 行，alps 原版 + panel_dead）
- `drivers/gpu/drm/mediatek/mediatek_v2/mtk_dump.h`（+3 行）
- `drivers/gpu/drm/mediatek/mml/Makefile`（恢复 + dbgtp）
- `drivers/gpu/drm/mediatek/{Makefile, dummy_drm/Makefile, panel/Makefile}`
- 外围：slbc/hwccf/dma-buf/heaps/sound/gpufreq/hal/soc/mediatek/vmm/mmdebug
