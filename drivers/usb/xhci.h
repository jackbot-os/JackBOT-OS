#ifndef JACKBOT_DRV_XHCI_H
#define JACKBOT_DRV_XHCI_H

#include <stdint.h>

/* One entry of the kernel's USB device table, filled in by the xHCI probe. */
struct usb_dev_info {
    uint16_t vid;
    uint16_t pid;
    uint8_t  dev_class;
    uint8_t  dev_subclass;
    uint8_t  dev_protocol;
    uint8_t  speed;         /* xHCI port speed id: 1 FS, 2 LS, 3 HS, 4 SS */
    uint8_t  port;          /* 1-based root port */
    uint8_t  slot;          /* xHCI slot id assigned to this device */
    uint16_t max_packet0;
};

/* Bring up an xHCI controller at the given BAR physical address and enumerate
   the devices that are already plugged in on its root ports. Returns the
   number of devices described, or a negative value on failure. */
int xhci_probe(uint32_t bar_phys, struct usb_dev_info *out, int max_out);

/* Identity of every device found, for the USB page in the GUI. */
int xhci_dev_count(void);
const struct usb_dev_info *xhci_dev_get(int idx);

/* Hand the probe result to the driver table the GUI reads. */
void xhci_store_devices(const struct usb_dev_info *list, int n);

/* Control transfers issued after the probe (WLAN, HID, storage drivers). The
   data buffer must be the pointer from xhci_scratch(): the controller
   addresses that page physically. */
void *xhci_scratch(void);
int xhci_scratch_size(void);
int xhci_ctrl_in(uint8_t slot, uint8_t req, uint16_t val, uint16_t idx,
                 void *buf, uint16_t len);
int xhci_ctrl_out(uint8_t slot, uint8_t req, uint16_t val, uint16_t idx,
                  const void *buf, uint16_t len);

#endif
