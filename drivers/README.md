# drivers/

设备驱动都在这里，一个 `.c` 一个驱动，配套 `.h`。内核 `kernel/` 里只保留
子系统（调度、内存、文件系统、UI），硬件寄存器操作一律放在这个目录，方便你
直接打开看。

| 目录 | 驱动 | 许可 / 来源 |
| --- | --- | --- |
| `usb/` | `xhci.c` — USB 3.x（xHCI）主机控制器：复位、命令环/事件环、端口上电与复位、Enable Slot、Address Device、取设备描述符 | 按 USB xHCI 1.1 规范实现；结构与状态机参照 FreeBSD `sys/dev/usb/controller/xhci.c`（BSD-2-Clause）与 TinyUSB `src/portable/ehci/ehci.c`（MIT），两者都在 `third_party/` 里有原文 |

编译规则在根目录 `Makefile` 的 `DRV_SRCS`：新增驱动只要加一行，就会编成
`build/drv_<路径>.o` 并链接进内核，不需要改 `KERN_SRCS`。

## 正在做的事

- `xhci.c`：目标是**真枚举**——不是只看端口状态位。它会给端口上电、发
  Enable Slot、下发 Address Device（含 EP0 上下文），再用控制传输读
  `GET_DESCRIPTOR(DEVICE)`，把 VID/PID/设备类打到串口，并在内存里留下
  一张设备表给 `app_usb` / 驱动匹配用。

## 实测记录（2026-11-10，VirtualBox 7.2.14 + Extension Pack）

真的枚举到了一个物理 USB 设备（主机上的 `1c4f:0048` USB 鼠标），串口原文：

```
[USB] hc0 kind=xHCI ports=0x000000000000000e
[XHCI] cap len=0x80 slots=0x20 ports=0xe ctx=0x20 hcs2=0x59 dboff=0x3000 rtsoff=0x2000
[XHCI] reset done sts=0x1
[XHCI] noop code=0x1
[XHCI] port1 reset seen=0 portsc=0x200803 speed=0x2
[XHCI] slot=0x1
[XHCI] device port=1 vid=0x1c4f pid=0x0048 class=0x0 speed=0x2 slot=0x1
```

调试中真实踩到并修掉的坑，按顺序：

1. **运行时寄存器基址算错**：`RTSOFF` 开头 `0x00..0x1F` 是 MFINDEX，中断寄存器组
   IR0 从 `+0x20` 开始。IMAN/ERSTSZ/ERSTBA/ERDP 全写进 MFINDEX，等于没登记
   事件环，所有命令都“超时失败”。
2. **命令环周期位**：CRCR 回读的 RCS 不可靠，按规范保持初始 CCS=1 才被消费。
3. **命令 TRB 缺 Slot ID**：Address Device 的 dw3 bits 24..31 必须填 slot，
   否则返回 `SLOT_NOT_ON (0x0B)`。

已知限制（都是明摆着的，不是猜）：不支持 hub（只能看见直插根口的设备）；没有
热插拔/中断处理，只在上电扫描时枚举一次，GUI 里点 Refresh 会重新扫；还没做
`SET_CONFIGURATION` 之后的类驱动（HID/存储/网卡）。

后续：`usb/` 里加 HID、存储、`wlan/` 加 USB 无线网卡（需要 802.11 栈，
FreeBSD `net80211` 是 BSD-2 但体量大，要单独排期），`bluetooth/` 接
NimBLE 的 HCI 传输。
