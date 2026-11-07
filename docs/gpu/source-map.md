# 文档来源地图（GPU）

记录时间：2026-11-07。每条都标注"能拿到什么 / 拿不到什么 / 本次实测结果"。

## NVIDIA —— 全闭源，但社区寄存器文档齐全

| 文档 | 地址 | 覆盖 | 缺口 |
| --- | --- | --- | --- |
| envytools MMIO 总图 | https://envytools.readthedocs.io/en/latest/hw/mmio.html | BAR0 寄存器空间划分（g80 家族 `0x1000000` = 16MB；GF100+ 同尺寸），各引擎窗口起址 | 窗口内个别寄存器仍未标注 |
| PMC（主控） | https://envytools.readthedocs.io/en/latest/hw/bus/pmc.html | PMC 窗口 `0x0–0x1000`；ID `0x0`、ENDIAN `0x4`、ENABLE `0x200`、SPOON_ENABLE `0x204`、INTR_* `0x100/0x140/0x160/0x640`、NEW_ID `0xa00`；各 bitfield 含义 | 部分 bit 标 TODO |
| GPU 家族表 | https://envytools.readthedocs.io/en/latest/hw/gpu.html | 家族划分（Tesla/Fermi/Kepler/Maxwell/…）、GPU id 与 PCI id 对照、GPC/TPC 数量 | 最新几代（Ada/Blackwell）覆盖不足 |
| PSTRAPS | https://envytools.readthedocs.io/en/latest/hw/io/pstraps.html | 板级配置 straps（显存类型、BAR 尺寸、晶振）读取与覆盖方法 | — |
| FIFO / Puller | https://envytools.readthedocs.io/en/latest/hw/fifo/puller.html | 命令提交模型：通道、方法（mthd）、信号量、REF_CNT | 具体各引擎方法表不全 |
| nouveau 项目 | https://nouveau.freedesktop.org/index.html | 现状说明（Kepler/Maxwell 支持状态） | 不含寄存器细节 |

**实测**：以上 5 页 2026-11-07 均可正常抓取，PMC 页给出的 bitfield 与本文档引用一致。

## Intel —— 开源驱动，手册曾公开但现在抓不到

| 文档 | 地址 | 状态（实测） |
| --- | --- | --- |
| Gen9 计算架构白皮书 | https://www.intel.cn/content/dam/develop/external/us/en/documents/the-compute-architecture-of-intel-processor-graphics-gen9-v1d0-740350.pdf | 可下载；讲 EU/Slice/带宽，**不讲寄存器偏移** |
| Haswell PRM（旧 PRM 卷） | https://01.org/sites/default/files/documentation/intel-gfx-prm-osrc-hsw-gpu-overview_0_0.pdf | **已失效**：抓取后返回 Intel "Open Ecosystem" 介绍页，PDF 不再提供 |
| 旧 PRM 索引（01.org/linuxgraphics/hardware-specification-prms） | https://01.org/linuxgraphics/hardware-specification-prms/2016-intelr-processors-based-kaby-lake-platform | 同上，01.org 已不提供原文件 |

**结论**：Intel 寄存器级要靠 PRM 镜像（第三方存档/离线 PDF）。没有 PRM 之前，不写 Intel 的寄存器序列——照白皮书猜偏移等于编。

## AMD —— 只公开着色器 ISA

| 文档 | 地址 | 覆盖 | 缺口 |
| --- | --- | --- | --- |
| GPUOpen 架构编程文档总入口 | https://gpuopen.com/amd-gpu-architecture-programming-documentation/ | ISA 规格、机器可读 XML | 不含显示/寄存器编程 |
| RDNA3.5 ISA 参考 | https://www.amd.com/content/dam/amd/en/documents/radeon-tech-docs/instruction-set-architectures/rdna35_instruction_set_architecture.pdf | 指令编码、wave 执行、资源描述 | 不涉及 BAR/寄存器 |
| 机器可读 ISA | https://gpuopen.com/machine-readable-isa/ | XML 指令表 + IsaDecoder | 同上 |

**结论**：AMD 目前只能做到"识别 + ISA 理解"，寄存器级驱动没有公开依据，不做。

## 通用（已有实现）

| 文档 | 地址 | 用途 |
| --- | --- | --- |
| VMware SVGA II 设备寄存器接口（VirtualBox `--graphicscontroller vmsvga` 同族） | 见 `kernel/svga.c` 头注释里记录的寄存器表 | 现有唯一有真实硬件可验证的 GPU 通路（`15ad:0405`） |

## 本次代码落点（2026-11-07，1.0.4-gpu-all）

| 文件 | 用到的文档事实 | 实现深度 |
| --- | --- | --- |
| `kernel/gpu.c` / `include/kernel/gpu.h` | 无外部文档；PCI class code `0x03` 分派 | BAR0 地址/尺寸测量（含 64 位 BAR）、逐页 MMIO 映射、三家 vendor 分派 |
| `kernel/gpu_nv.c` | `hw/gpu.html`（芯片表 66 条）、`hw/mmio.html`（BAR0 布局）、`hw/bus/pmc.html`（PMC 寄存器 + 中断位段）、`hw/io/pstraps.html`（straps 0x101000 寄存器与 G80+ 位段） | PMC 全只读采集（ID / NEW_ID / BOOT_2 / ENDIAN / INTR_HOST / INTR_MASK / ENABLE / SPOON_ENABLE）、gpuid 解析、引擎位解码（G80:GF100 与 GF100+ 两表）、PSTRAPS set0/set1 读取并解码（device_id、晶振、VBIOS 类型、BAR0/BAR1 尺寸码、PCI class）；**零写入** |
| `kernel/gpu_intel.c` | 无（PRM 失效） | 只做 GTTMMADR / MMIO BAR / subsystem 孔径实测，不写寄存器序列 |
| `kernel/gpu_amd.c` | 无（只有 ISA） | 只做 BAR0/2/5 与 subsystem 孔径实测 |

**验证状态**：`make iso` 零 C 告警（唯一告警是链接期 RWX segment）；在 VirtualBox 上启动到 `[GPU] dev ... class=0x03` 与 `[STEP] splash`。VirtualBox 不模拟 NVIDIA/Intel/AMD 显卡，因此三家分支的寄存器读取**尚未在实体机上验证**（三家的代码路径在 VM 中不可达）。

## 许可口径（2026-11-07 用户更新）

可用面：**除 GPL 代码外均可取用**（MIT/BSD/Apache/公共领域/公开文档事实）。仍禁止：GPL/AGPL 源码、换名移植 GPL 代码、反编译厂商二进制驱动或 VBIOS。envytools 项目自身的许可证未在本次实测中确认，因此本项目只取其文档中的**寄存器事实**（地址、位段、语义），不复制其文档正文与代码。
