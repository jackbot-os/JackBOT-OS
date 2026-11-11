# Vendored driver sources — FreeBSD (BSD-2-Clause)

All files under `drivers/vendor/freebsd/` are **downloaded verbatim from the internet**,
not written by hand. Nothing here is a stub or a placeholder.

- Upstream: FreeBSD base source tree (`freebsd/freebsd-src`).
- Mirrors actually reachable from this build host: `https://gitee.com/mirrors/FreeBSD/raw/main/`
  for the files under `sys/dev` and `sys/net80211`, and `https://cgit.freebsd.org/src/plain/`
  (the official FreeBSD cgit, `?h=releng/13.2`) for the bluetooth stack, which is no longer
  present on `main` and is not carried by the gitee mirror's partial tree.
  (raw.githubusercontent.com times out here; jsDelivr returns 403 for this repo.)
- License: BSD-2-Clause for every file listed below. Full text kept in `COPYRIGHT`
  in this directory. No GPL/LGPL/AGPL code is vendored (banned by project policy).

## What was fetched

| Local path | Bytes | Upstream path |
| --- | --- | --- |
| `COPYRIGHT` | 6070 | `COPYRIGHT` (FreeBSD copyright notice) |
| `sys/dev/sound/pci/hda/hdac.c` | 63915 | `sys/dev/sound/pci/hda/hdac.c` |
| `sys/dev/sound/pci/hda/hdac.h` | 52098 | `sys/dev/sound/pci/hda/hdac.h` |
| `sys/dev/sound/pci/hda/hdac_private.h` | 7903 | `sys/dev/sound/pci/hda/hdac_private.h` |
| `sys/dev/sound/pci/hda/hda_reg.h` | 59167 | `sys/dev/sound/pci/hda/hda_reg.h` |
| `sys/net80211/ieee80211.c` | 78759 | `sys/net80211/ieee80211.c` |
| `sys/net80211/ieee80211_var.h` | 49334 | `sys/net80211/ieee80211_var.h` |
| `sys/net80211/ieee80211_input.c` | 30235 | `sys/net80211/ieee80211_input.c` |
| `sys/net80211/ieee80211_output.c` | 128532 | `sys/net80211/ieee80211_output.c` |
| `sys/net80211/ieee80211_node.c` | 97337 | `sys/net80211/ieee80211_node.c` |
| `sys/dev/usb/wlan/if_run.c` | 172758 | `sys/dev/usb/wlan/if_run.c` (Ralink/MediaTek 802.11 USB) |
| `sys/dev/usb/wlan/if_rsu.c` | 98467 | `sys/dev/usb/wlan/if_rsu.c` (Ralink 802.11 USB) |
| `sys/dev/iwm/if_iwm.c` | 198279 | `sys/dev/iwm/if_iwm.c` (Intel 802.11 PCIe) |
| `sys/dev/usb/net/if_ure.c` | 64232 | `sys/dev/usb/net/if_ure.c` (USB Ethernet) |
| `sys/dev/usb/net/if_axe.c` | 40573 | `sys/dev/usb/net/if_axe.c` (ASIX USB Ethernet) |
| `sys/dev/usb/net/usb_ethernet.c` | 14865 | `sys/dev/usb/net/usb_ethernet.c` |
| `sys/dev/usb/usb_transfer.c` | 97272 | `sys/dev/usb/usb_transfer.c` (USB transfer machinery) |
| `sys/dev/usb/usb_core.c` | 2563 | `sys/dev/usb/usb_core.c` |
| `sys/netgraph/bluetooth/drivers/ubt/ng_ubt.c` | 52065 | `sys/netgraph/bluetooth/drivers/ubt/ng_ubt.c` @ releng/13.2 |
| `sys/netgraph/bluetooth/drivers/ubt/ng_ubt_var.h` | 5896 | `sys/netgraph/bluetooth/drivers/ubt/ng_ubt_var.h` @ releng/13.2 |
| `sys/netgraph/bluetooth/common/ng_bluetooth.c` | 7870 | `sys/netgraph/bluetooth/common/ng_bluetooth.c` @ releng/13.2 |
| `sys/netgraph/bluetooth/include/ng_bluetooth.h` | 6016 | `sys/netgraph/bluetooth/include/ng_bluetooth.h` @ releng/13.2 |
| `sys/netgraph/bluetooth/include/ng_hci.h` | 79283 | `sys/netgraph/bluetooth/include/ng_hci.h` @ releng/13.2 |

## Coverage against the requested driver set

| Requested | Source state |
| --- | --- |
| Sound (HDA + AC'97) | HDA source vendored; AC'97 already native in `kernel/ac97.c` |
| WiFi (802.11) | 802.11 core + 3 hardware drivers vendored |
| Ethernet (USB) | 3 files vendored; RTL8139 already native in kernel |
| USB transport | transfer/core vendored; xHCI native in `drivers/usb/xhci.c` |
| Bluetooth | USB HCI transport (`ng_ubt`) + HCI definitions vendored from the 13.2 branch |
| GPU | vendor native only (VMSVGA in `kernel/svga.c`) |

## Bluetooth: found, with a note on where

The bluetooth stack is not on `main` any more and not in the gitee mirror's partial
tree, so the earlier "404 everywhere" conclusion was a path problem, not an absence:
FreeBSD keeps it under `sys/netgraph/bluetooth/...` and it survives on `releng/13.2`.
Five files were fetched from the official cgit (`cgit.freebsd.org`, `?h=releng/13.2`).

Still not vendored: the HCI protocol node body (`ng_hci.c`) and the L2CAP node
(`ng_l2cap.c`). `sys/netgraph/bluetooth/hci/` on 13.2 contains only a `TODO` file, and
the obvious plain paths return 404 — their real location still has to be located in the
cgit tree before those two can be fetched. The USB host-controller transport, which is
what actually brings a dongle up, is already in hand.

## Porting status

These sources are FreeBSD kernel code. They depend on FreeBSD's `newbus`, `bus_dma`,
`mtx`, `callout`, `sx` and `malloc` types, which this kernel does not provide. So
vendoring is step one; each driver still needs a compatibility shim layer
(`drivers/compat/`) plus an adapter before it can drive real hardware.

Nothing in this directory is compiled into the ISO yet.
