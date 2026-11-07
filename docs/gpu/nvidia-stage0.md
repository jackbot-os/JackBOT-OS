# NVIDIA 第一阶段规格（只读识别）

依据：envytools 文档（PMC / MMIO / GPU 家族表），全部为公开页面。
本节只描述**已经在代码里实现**的部分，以及下一阶段需要的寄存器依据。

## 已实现：`kernel/gpu_nv.c`

| 步骤 | 动作 | 寄存器依据 |
| --- | --- | --- |
| 1 | PCI 扫描 vendor `0x10DE` + class `0x03`（显示控制器） | 通用 PCI |
| 2 | 取 BAR0（memory BAR），映射 16 页（64 KiB）uncached（`PTE_PCD|PTE_PWT`） | BAR0 MMIO 空间 g80+ 为 `0x1000000`（16 MB） |
| 3 | 读 `PMC.ID`（BAR0+`0x0`）与 `PMC.NEW_ID`（BAR0+`0xa00`） | 见下方 bitfield |
| 4 | 解 GPU id / stepping / device id，映射家族名 | 家族表 |
| 5 | 读 `PMC.ENABLE`（`0x200`）、`PMC.INTR_HOST`（`0x100`），并把 ENABLE 里打开的引擎（PPPP/PXBAR/PMFB/PRING/PFIFO/PGRAPH/PDAEMON/PTIMER/PBFB/PCOUNTER/PFFB/PDISPLAY）按名字打出来 | 引擎使能位图 / 中断状态 |

**零写入**：这一段不写任何寄存器，所以在真卡上也不会把显卡打成砖。

### bitfield（envytools PMC 页原文）

`PMC.NEW_ID`（G94 起）：

- bits 0–7：device id
- bits 8–11：与 `BOOT_2` 同值
- bits 12–19：stepping
- bits 20–27：GPU id

`PMC.ID`（NV10 起，老卡回退用）：

- bits 0–7：stepping
- bits 12–19：device id（GF119 起位置有变，代码里按 12–19 取）
- bits 20–27：GPU id

### 家族表（只写文档里核对过的项，其余打印 `unmapped`）

| GPU id | 家族 |
| --- | --- |
| `0xC0` | GF100（Fermi） |
| `0xC1` | GF108 |
| `0xC3` | GF106 |
| `0xC4` | GF104 |
| `0xCE` | GF114 |
| `0xCF` | GF116 |

Kepler（GK104/GK107/GK208 等）在 envytools 家族表里存在，但**GPU id 字段的取值本文没有逐个核对**，所以代码里暂不硬编码，只打印原始 `gpuid`。下次拿到真卡读数后补表——不猜。

## 下一阶段（未实现，按依赖顺序）

1. **PMC.ENABLE 引擎使能**（`0x200`）：按文档，某位置 0 会让对应引擎从 MMIO 消失并复位；位置 1 才可访问。GF100+ 的位段：`1=PPPP`、`2=PXBAR`、`3=PMFB`、`5=PRING`、`8=PFIFO`、`12=PGRAPH`、`13=PDAEMON`、`16=PTIMER`、`20=PBFB`、`28=PCOUNTER`、`29=PFFB`、`30=PDISPLAY`。
2. **PFIFO 初始化**：需要 FIFO 窗口内寄存器与通道表（GF100+ 的 `PFIFO_CHAN` 在 BAR0+`0x800000`），文档覆盖到"通道/子通道/方法"模型，但**具体初始化值仍需真卡试验**。
3. **显存搬运**：GF100+ 的 `m2mf` 已被 `p2mf` / PCOPY 取代，命令提交通道是文档里描述最细的一块。
4. **显示输出**：PDISPLAY 窗口（`0x610000` 起），这块文档标注较少，风险最高。

## 构建验证（2026-11-07 14:51）

- `make iso` 在 Debian 构建机内跑完：`MAKE_EXIT=0`，`kernel/gpu_nv.c` 编译零告警（全工程唯一告警仍是链接期 `LOAD segment with RWX permissions`）。
- 产物 `build/JackBOT-1.0.4.iso` = 41,455,616 字节，已回传为 `E:\JackBOT-OS\JackBOT-1.0.4-gpu-nv.iso`。
- **未做开机验证**：本次没有启动测试机（另一条 AHCI 测试正占着 VirtualBox，我不去抢它的电源）。因此 `[NV] ...` 这行在 VM 里的实际打点尚未回读。

## 验证条件（硬约束）

- 只能在带 NVIDIA PCI 功能的实体机上验证。当前可用的测试机里，VirtualBox 不模拟 NVIDIA 显卡，所以 VM 里这段只会打印 `[NV] no NVIDIA GPU (10de class 03) here`。
- 用户手上的 ASUS W419L 是 Intel 核显 + NVIDIA 独显的组合，属于后续真机验证的候选，但需要把 ISO 装到那台机器上跑。
