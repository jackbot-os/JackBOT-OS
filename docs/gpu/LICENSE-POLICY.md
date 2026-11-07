# 驱动许可与清洁室协议（硬性）

**这一条是红线，优先于任何进度要求。**

## 结论先行

JackBOT OS 的驱动代码（`kernel/gpu_nv.c`、后续 `kernel/gpu_*.c`、`kernel/ahci.c` 等）
**只允许是原创实现**，不得包含、改写、翻译、"转换格式"任何 GPL 代码。
一旦沾上 GPL，整个内核的授权状态和商业分发都会被拖死——这是不可逆的。

## 禁止清单（一条都不能碰）

| 禁止 | 具体对象 |
| --- | --- |
| 复制 GPL 源码 | Linux 内核 `drivers/gpu/drm/nouveau/`、`drivers/gpu/drm/amd/amdgpu/`、`drivers/gpu/drm/i915/`、`drivers/ata/` 等 |
| "改写"GPL 代码 | 换变量名/换函数签名/换注释/机翻之后再落地，同样算衍生作品 |
| 搬 Linux 基础设施 | `struct device`、`drm_*`、`i2c_*`、`firmware loader`、`kthread`、`workqueue`、`kref` 等 |
| 反编译/抽取二进制 | 厂商驱动 DLL/sys、VBIOS、显卡固件里抠寄存器和表 |
| 复制文档正文 | 把文档段落整段贴进代码注释当"依据"（抄表达而非抄事实） |

## 允许清单

| 允许 | 说明 |
| --- | --- |
| 公开文档里的**事实** | 寄存器偏移、位段位置、寄存器语义、设备 ID、时序要求。事实不受版权保护 |
| 自己推导的实现 | 初始化顺序、轮询/超时策略、数据结构、错误处理，全部自己写 |
| 公开的社区文档作为线索 | envytools（MIT 项目）等，只取事实，不取代码 |
| 现有自研代码 | `pci_*` / `pmm_*` / `vmm_*` / `serial_*` 这一套 JackBOT 自己的 API |

## 记录规则（每个驱动文件都要能追溯）

新增/修改驱动文件时，在该文件顶部写明来源与隔离方式，例如：

```
/* Written for JackBOT OS from public register descriptions.
   No GPL/Linux-derived code. Facts (offsets/bitfields) only; no doc text copied. */
```

并在本目录 `source-map.md` 的表格里登记：用了哪份文档、取了哪些事实、**没有**取什么。

## 已登记文件

| 文件 | 事实来源 | 隔离结论 |
| --- | --- | --- |
| `kernel/gpu_nv.c` | envytools PMC / MMIO / GPU 家族页（公开网页） | 只取寄存器偏移与位段位置；代码、结构、函数命名、打印格式全部自写；未引用任何 GPL 项目代码 |
| `kernel/ahci.c` | AHCI 1.3.1 规范（Intel 公开 PDF） | 只取寄存器布局与命令语义；PRDT/命令表结构与轮询策略自写 |
| `kernel/svga.c` | VMware SVGA II 设备接口（公开寄存器表） | 同上 |

## 如果以后要引第三方代码

先确认许可证允许静态链接进闭源/自有内核（MIT / BSD / Apache-2.0 可以，GPL / LGPL 不行），
并在 `third_party/` 下留原始许可证文件与出处。**默认不引**。
