# third_party — 外部实现来源与许可台账

本目录只放**许可允许闭源分发**的第三方实现。规则：BSD-2/3、MIT、Apache-2.0、
ISC、zlib、Public Domain 可以用；GPL/LGPL/AGPL（任何"必须公开源代码"的
copyleft）一律不进树，也不做"只参考不抄"式的改写。

| 目录 | 上游 | 许可 | 入树内容 | 状态 |
| --- | --- | --- | --- | --- |
| `nimble/` | apache/mynewt-nimble（master 快照，2026-11-10 下载） | Apache-2.0 | `nimble/host`、`nimble/controller`、`nimble/include`、`porting/` | 源码已入树，**尚未接到 JackBOT 内核** |
| `tinyusb/` | hathach/tinyusb（master 快照，2026-11-10 下载） | MIT | `src/`（host + device + common）、`hw/` | 源码已入树，**尚未接到 JackBOT 内核** |
| `cpython/` | python/cpython | PSF License（宽松，非 copyleft） | 官方源码 | 已有移植层骨架 |

许可是**逐字校验**过的：`LICENSE` 原文就在各自目录里，两个新增目录内
`GNU General Public License` 命中数为 0（在 `third_party` 下扫描时只有
`cpython/` 里的 autoconf/许可证说明文档提到 GPL，不是 GPL 代码）。

## 为什么选这两个而不是 Linux/BlueZ

- Linux `drivers/usb`、`net/mac80211`、`net/bluetooth` 是 **GPL-2.0**，按你的红线
  不能用，也不能"改写后再声称不是衍生"。
- BlueZ 是 GPL-2.0，同样排除。
- NimBLE 是 Apache-2.0 的完整蓝牙 host（HCI→L2CAP→ATT→GATT→SM），自带
  `porting/` 目录专门给出裸机 OS 抽象样板（mutex/mbuf/log），比 Zephyr 的
  BT host 更容易塞进无 MMU、无调度器的内核。
- TinyUSB 是 MIT 的 USB host 栈，`src/host` 里有 EHCI/OHCI 主机控制器驱动和
  枚举/描述符解析，正好补上 `kernel/usb.c` 缺失的"控制传输 + 描述符 + 端点"。

## 接入顺序（下一步，按依赖排序）

1. `kernel/usbhost_port.c`：把 TinyUSB 的 OSAL（`osal_none`）对到 JackBOT 的
   临界区/延时；EHCI 控制器内存与 PCI BAR 用现有 `pci.c` 挂上去。
2. 枚举出设备后，用 `tuh_*` 拿描述符，先在串口打印 VID/PID/类，替换现在
   只有"根集线器端口状态"的 `usb.c`。
3. 蓝牙：NimBLE 的 HCI transport 换成"USB bulk/interrupt 端点读写"（FreeBSD
   `ng_ubt` 的分片/事件处理逻辑可作对照，BSD-2），再跑 `ble_hs` 初始化。
4. WiFi：802.11 这一层目前**没有任何可用开源实现**进了树——FreeBSD `net80211`
   是 BSD-2 但体量巨大（关联/加密/扫描整栈），`urtwn`/`rtwn` 依赖它。这一项
   需要单独排期，不能顺手做掉。

## 许可红线备忘

- 不许：GPL-2/3、LGPL、AGPL、任何 "must disclose source" 条款。
- 允许：BSD-2/3、MIT、Apache-2.0、ISC、zlib、Public Domain。
- 入树时必须连 `LICENSE` 一起放，并在本表登记来源与下载日期。
