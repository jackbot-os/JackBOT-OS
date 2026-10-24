#ifndef JACKBOT_USB_H
#define JACKBOT_USB_H
/*
 * JackBOT OS USB 子系统
 * 枚举 PCI 上的 USB 主控制器（UHCI/OHCI/EHCI/xHCI），并通过各控制器
 * 根集线器（Root Hub）端口寄存器真实读取端口连接/速度/供电/过流状态。
 * 注意：这里做的是“主控发现 + 根集线器端口状态”，完整的设备枚举
 * （控制传输、描述符解析、HID/大容量存储类驱动）是后续阶段。
 */
#include <stdint.h>

#define USB_KIND_UHCI    0
#define USB_KIND_OHCI    1
#define USB_KIND_EHCI    2
#define USB_KIND_XHCI    3
#define USB_KIND_UNKNOWN 4

#define USB_MAX_HC    8
#define USB_MAX_PORTS 16

/* 端口速度 */
#define USB_SPD_NONE   0
#define USB_SPD_LOW    1   /* USB 1.x 低速 1.5 Mbps  */
#define USB_SPD_FULL   2   /* USB 1.x 全速 12 Mbps   */
#define USB_SPD_HIGH   3   /* USB 2.0 高速 480 Mbps  */
#define USB_SPD_SUPER  4   /* USB 3.x 超高速 5 Gbps  */

struct usb_hc {
    uint8_t present;
    uint8_t bus, dev, func;
    uint8_t kind;
    uint16_t vendor;
    uint16_t device;
    uint8_t irq;
    uint8_t nports;
    uint16_t io_base;    /* UHCI 的 I/O 基址 */
    uint32_t mem_base;   /* OHCI/EHCI/xHCI 的 MMIO 物理基址 */
    uint8_t conn[USB_MAX_PORTS];  /* 端口是否有设备连接 */
    uint8_t speed[USB_MAX_PORTS]; /* USB_SPD_* */
    uint8_t pwr[USB_MAX_PORTS];   /* 端口是否供电 */
    uint8_t oc[USB_MAX_PORTS];    /* 端口过流标志 */
};

/* 重新扫描所有 USB 主控并读取根集线器端口状态，返回主控数量。 */
int usb_scan(void);
int usb_hc_count(void);
const struct usb_hc *usb_hc_get(int idx);
int usb_total_connected(void);

const char *usb_kind_name(int kind);
const char *usb_kind_full_name(int kind);
const char *usb_speed_name(int s);

#endif
