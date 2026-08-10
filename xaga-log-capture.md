# xaga 内核日志捕获方法（LK log_store 恢复 + XAGR 环）

> 真机验证状态：2026-08-10 实测通过——6.12 内核把 printk 镜像进 log_store 保留区的
> XAGR 环，LK 在下次启动时把 log_store 区内容恢复记录到 expdb，内核日志随 expdb
> 重新出现（含 MIRROR:n 心跳证明镜像存活）。

## 一句话原理

**LK 的 PL_LOG_STORE 机制会在启动时把 DRAM log_store 保留区（0x7ffbf000, 256KB）的
内容恢复记录到 expdb 分区**——因此把内核日志写进 log_store 区，就能让 LK 替我们
记录内核日志，重启后从 expdb 读回。**不需要自建读端，也不受 console 配置影响。**

## 机制细节（2026-08-10 实证）

- log_store 区 = LK mblock-R 保留区：`0x7ffbf000 size 0x40000 map:1 name:log_store`。
- LK 启动第一行日志 `PL_LOG_STORE: set ram_header->sig = 0xABCD1234` = LK 初始化该区。
- 真机证据：expdb 内核日志区间里出现 `stage=1`（无 printk 前缀）——那是 XAGR 环
  文本被 LK 恢复出来的，紧跟在 `xaga-marker-writer: XAGR ring armed`（printk）之后。
- expdb 的内核日志区间 = 内核 log buffer 抓取（printk，从 setup_arch 头的
  "XAGR ring armed" 开始）+ log_store 区恢复内容（环文本）拼接。

## 用法

1. 刷入内置了 XAGR 环写端的内核（`CONFIG_XAGA_MARKER_WRITER=y`）。
2. 启动到任意阶段（含挂死/panic/WDT 重启）。
3. 重启后 dump expdb 分区（LK 侧方式），内核日志尾部就是最后一段 printk +
   环内容（`MIRROR:n` 心跳、`stage=n` 标记）。

## 写端实现（K 树 `drivers/misc/xaga-marker-writer.c`，内置）

- `xaga_marker_early_init()`：`setup_arch` 头部（`early_fixmap_init()` +
  `early_ioremap_init()` 之后第一件事）`early_ioremap(0x7ffbf000, 0x10000)`，
  重置 magic/cursor/total/stage，写 `stage=1`。这是 arm64 MMU 允许的**最早**映射点
  （paging_init 前线性映射未建立，0x7ffbf000 无映射，无法更早）。
- `xaga_marker_early_printk()`：`vprintk_emit()`（kernel/printk/printk.c）顶部钩子，
  每个 printk 经 `va_copy + vscnprintf` 镜像进环（滚动 56KB，无锁无分配，任意
  printk 上下文安全，panic 后消息也入环）。每 64 条写 `MIRROR:n` 心跳。
- 环布局（与 lineage 读端一致）：`u32 magic 0x52474158 @0x0000 / cursor @0x0004 /
  total @0x0008 / stage @0x1000 / 文本环 @0x2000（0xE000 字节）`。
- 每次 ring_write 重断言 magic（防 aee/mrdump 覆写 header）。
- module notifier 保留：vendor 模块 probe 挂死时最后一条 = 模块名。

## 已排除的候选区（都不能用）

| 区 | 地址 | 原因 |
|---|---|---|
| minirdump | 0x48170000 | 写入触发 MTK mrdump 机制**立即重启**（2026-08-09 实测） |
| pstore | 0x48090000 | 内核 ramoops 占用（`ramoops: using 0xe0000@0x48090000`） |
| aee_lk | 0x50700000 | **LK 不恢复该区**（环放这里 expdb 无内容，2026-08-10 实测） |
| gz-log / atf-log | 0x7f200000 / 0xbfe00000 | TEE/ATF 保留区，NS 访问 SError panic |
| log_store 本身 | 0x7ffbf000 | **唯一可用**（LK 自动恢复记录） |

## 坑

- **"setup_arch 前"在 arm64 物理上不可行**：paging_init 前 0x7ffbf000 无映射；
  `early_ioremap`（fixmap，earlycon 同款机制）是硬件允许的最早途径，映射持久。
- **`CONFIG_EARLY_PRINTK` 在 arm64 是死代码**：printk.h 里是空函数、无 arm64
  Kconfig、无人注册 `early_console`。真实早期打印流 = `vprintk_emit` 钩子。
- **`early ioremap leak of 1 areas` 警告是正常的**（故意保留映射供全程写环）。
- **merge_config -m 只加不改**：关闭 gki 基的符号（KVM/特性/KASAN）必须在
  build.sh config() 里 sed 覆盖并加 if-grep 断言（`grep && {...}` 在 set -e 下
  grep 不匹配会杀脚本，须用 if 形式）。
- **expdb 抓取的内核日志从 setup_arch 头开始**（"XAGR ring armed"），更早的
  banner/start_kernel 早期 printk 不在（log buffer 抓取起点）。
- 6.12 移植链路上的其他启动修复（与本方法配套，缺一不可）：
  PKVM 移除（xaga 无硬件虚拟化）、SMCCC TRNG/SOC_ID 探测守卫、aee_aed 模块
  （否则 init first-stage 加载 aee_rs 缺 `aee_get_mode` 符号 abort）。

## 相关文件

- 写端：K 树 `drivers/misc/xaga-marker-writer.c` + `include/linux/xaga_marker.h`
- 钩子：K 树 `kernel/printk/printk.c`（vprintk_emit 顶部）+ `arch/arm64/kernel/setup.c`
  （setup_arch 头调 `xaga_marker_early_init()`）
- 读端（备用，非必需）：lineage_xaga `drivers/misc/xaga-marker.c`
- 构建接线：移植树 `build.sh`（config sed + 模块断言）+ `arch/arm64/configs/vendor/xaga.config`
