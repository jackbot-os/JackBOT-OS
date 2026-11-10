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
- 后续：`usb/` 里加 HID、存储、`wlan/` 加 USB 无线网卡（需要 802.11 栈，
  FreeBSD `net80211` 是 BSD-2 但体量大，要单独排期），`bluetooth/` 接
  NimBLE 的 HCI 传输。
