# GPU 驱动（clean-room）工作底稿

这个目录是 JackBOT OS 显卡驱动的**文档来源与规格底稿**，不是宣传材料。
原则只有一条：**只用公开文档 + 公开的社区寄存器文档，自己写代码**。

## 边界（写死，别越线）

**许可红线另见 `LICENSE-POLICY.md`：驱动层不得沾任何 GPL 代码，这是不可逆风险，优先级高于进度。**

| 允许 | 禁止 |
| --- | --- |
| 厂商发布的公开手册、datasheet、ISA 参考 | 反编译厂商驱动 / VBIOS / 固件 |
| 社区公开的寄存器文档（envytools 等） | 抽取厂商二进制里的表、把闭源驱动"转换"成自己的 |
| 自己按文档推导的初始化序列 | 把 Linux 驱动代码搬进来（GPL 传染 + 依赖几千个 Linux API） |
| 读自己硬件上已存在的寄存器做识别 | 未经确认就往真卡上写寄存器 |

代码里凡是"照 Linux 抄"的部分都要重写成 JackBOT 自己的写法：只用 `pci_*` / `pmm_*` / `vmm_*` / `serial_*` 这一套，
不引入 Linux 的 `struct device`、`drm_*`、`i2c_*`、`firmware loader` 之类。

## 三家的文档可得性（实测，不是听说）

| 厂商 | 闭源程度 | 公开文档现状 | 能到哪一步 |
| --- | --- | --- | --- |
| NVIDIA | 全闭源 | 社区寄存器文档很细（PMC / PFIFO / PGRAPH / PFB，含 bitfield） | 识别芯片 → 引擎使能 → FIFO/通道，寄存器级可行；着色器微码与固件仍缺 |
| Intel | 开源驱动，手册曾是公开的 | 老的 PRM 链接**已失效**（见 source-map 实测）；架构白皮书仍在 | 识别 + 显示管线（按架构白皮书推导，PRM 需另找镜像） |
| AMD | 部分公开 | 只公开**着色器 ISA**（RDNA3.5 PDF / 机器可读 XML），不公开显示与寄存器编程 | 只能做识别与 ISA 级理解，寄存器级缺依据 |

结论：**先做 NVIDIA**（唯一有寄存器级公开线索的闭源厂商），Intel 次之（要先找到 PRM 镜像），AMD 暂不碰寄存器。

## 已完成（本目录对应代码）

- `kernel/gpu_nv.c` + `include/kernel/gpu_nv.h`：NVIDIA 第一阶段**只读识别**（读 PMC.ID / PMC.NEW_ID / PMC.ENABLE / INTR_HOST，解出 GPU id、stepping、device id，打印家族名）。零写入，真卡上也安全。
- 接入点：`kernel/main.c` 的 GPU takeover 段，`pci_enumerate()` 之后调用 `gpu_nv_probe()`。
- 构建：`Makefile` 的 `KERN_SRCS` 加 `gpu_nv`。

## 还没做（要按顺序啃）

1. NVIDIA：PMC.ENABLE 引擎使能 → PFIFO 初始化 → 一条 DMA 通道 → 显存搬运（`docs/gpu/nvidia-stage0.md` 里列了寄存器依据）。
2. Intel：先补到 Haswell(Gen7.5) 的 PRM 镜像，再做显示管线接管帧缓冲。
3. AMD：只做到识别 + 把 ISA 文档归档，不写寄存器级驱动。

## 验证前提（必须说清楚）

VirtualBox **不模拟** NVIDIA / Intel / AMD 的 PCI 显卡（只有 `15ad:0405` VMware SVGA）。
所以 `gpu_nv.c` 在 VM 里永远走"未找到显卡"分支，真值只能在**装 JackBOT OS 的实体机**上打出来。
这是硬约束，不是进度问题。
