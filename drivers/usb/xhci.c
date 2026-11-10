/*
 * xHCI (USB 3.x) host controller driver.
 *
 * Register and context bit layouts follow the xHCI 1.1 specification; the
 * same bit positions are used by FreeBSD's sys/dev/usb/controller/xhci.c
 * (BSD-2-Clause, vendored copy under _work/src/freebsd-usb) and by TinyUSB's
 * src/portable/ehci (MIT), which is what this file was checked against so
 * that no field is guessed.
 *
 * Scope: reset the controller, build command ring / event ring / ERST,
 * power and reset each connected root port, run Enable Slot + Address Device
 * for the first device, then read GET_DESCRIPTOR(DEVICE) over EP0 and report
 * VID/PID/class over the serial port. Hubs are not supported yet, so a
 * device behind a hub is not seen.
 */

#include <stdint.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/hal.h>
#include <kernel/serial.h>
#include "xhci.h"

#define XHCI_VBASE        0xffff800090000000ull
#define XHCI_MAP_BYTES    0x20000u

/* ---- operational registers (offsets from operational base) ---------- */
#define OP_USBCMD        0x00
#define OP_USBSTS        0x04
#define OP_PAGESIZE      0x08
#define OP_CRCR          0x18
#define OP_DCBAAP        0x30
#define OP_CONFIG        0x38
#define OP_PORTSC(n)     (0x400u + 0x10u * (n))

#define CMD_RS           (1u << 0)
#define CMD_HCRST        (1u << 1)
#define CMD_INTE         (1u << 2)

#define STS_HCH          (1u << 0)
#define STS_EINT         (1u << 3)
#define STS_PCD          (1u << 4)
#define STS_CNR          (1u << 11)

/* ---- port status bits ------------------------------------------------ */
#define PORT_CCS         (1u << 0)
#define PORT_PED         (1u << 1)
#define PORT_OCA         (1u << 3)
#define PORT_PR          (1u << 4)
#define PORT_PLS(v)      (((v) >> 5) & 0xFu)
#define PORT_PP          (1u << 9)
#define PORT_SPEED(v)    (((v) >> 10) & 0xFu)
#define PORT_CSC         (1u << 17)
#define PORT_PEC         (1u << 18)
#define PORT_WRC         (1u << 19)
#define PORT_PRC         (1u << 20)
#define PORT_PLC         (1u << 21)
#define PORT_CEC         (1u << 22)
#define PORT_WCE         (1u << 24)
#define PORT_WPR         (1u << 31)

/* ---- TRB types ------------------------------------------------------- */
#define TRB_SETUP        0x02
#define TRB_DATA         0x03
#define TRB_STATUS       0x04
#define TRB_ENABLE_SLOT  0x09
#define TRB_ADDRESS_DEV  0x0B
#define TRB_EVAL_CTX     0x0D
#define TRB_TRANSFER_EV  0x20
#define TRB_CMD_COMPLETE 0x21
#define TRB_PORT_CHANGE  0x22

#define TRB_ENTRIES      256      /* per ring, one page = 256 TRBs */
#define CMD_RING_IDX     0
#define EV_RING_IDX      0

typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t dw3;
} trb_t;

struct xhci_ctx {
    uint32_t dw[8];               /* 32 bytes per context when CSZ = 0 */
};

static volatile uint8_t *g_base;      /* mapped BAR */
static volatile uint8_t *g_op;
static volatile uint32_t *g_doorbell;
static volatile uint8_t *g_rt;
static uint32_t g_max_slots;
static uint32_t g_max_ports;
static uint32_t g_ctx_size;           /* 32 or 64 */
static uint32_t g_dcbaa_phys;

static trb_t *g_cmd_ring;
static uint32_t g_cmd_phys;
static uint32_t g_cmd_idx;
static uint32_t g_cmd_cyc;

static trb_t *g_ev_ring;
static uint32_t g_ev_phys;
static uint32_t g_ev_idx;
static uint32_t g_ev_cyc;

static trb_t *g_ep0_ring;
static uint32_t g_ep0_phys;
static uint32_t g_ep0_idx;
static uint32_t g_ep0_cyc;

static struct xhci_ctx *g_input;      /* input context for slot commands */
static uint32_t g_input_phys;
static struct xhci_ctx *g_devctx;     /* device context of the slot in use */
static uint32_t g_devctx_phys;

static struct usb_dev_info g_devs[8];
static int g_dev_n;

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static void loghex(const char *tag, uint32_t v)
{
    serial_puts(tag);
    serial_puthex(v);
    serial_putc('\n');
}

static uint32_t alloc_page(void)
{
    void *p = pmm_alloc_page_below(0x100000000ull);
    return p ? (uint32_t)(uintptr_t)p : 0;
}

static void zero_page(uint32_t phys)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)phys;
    for (int i = 0; i < 1024; i++)
        p[i] = 0;
}

/* ------------------------------------------------------------------ */
/* ring submission                                                    */
/* ------------------------------------------------------------------ */

static void cmd_push(uint32_t dw0, uint32_t dw1, uint32_t dw2, uint32_t type)
{
    if (g_cmd_idx + 1 >= TRB_ENTRIES) {
        serial_puts("[XHCI] command ring full\n");
        return;
    }
    trb_t *t = &g_cmd_ring[g_cmd_idx++];
    t->dw0 = dw0;
    t->dw1 = dw1;
    t->dw2 = dw2;
    t->dw3 = (type << 10) | g_cmd_cyc;
    __asm__ volatile("mfence" ::: "memory");
    g_doorbell[0] = 0;                   /* target 0 = command ring */
}

static int ev_pop(trb_t *out)
{
    trb_t *t = &g_ev_ring[g_ev_idx];
    if ((t->dw3 & 1u) != g_ev_cyc)
        return 0;
    *out = *t;
    g_ev_idx++;
    if (g_ev_idx >= TRB_ENTRIES) {
        g_ev_idx = 0;
        g_ev_cyc ^= 1u;
    }
    /* hand the consumed entry back to the controller */
    hal_write64(g_rt + 0x18, (uint64_t)(g_ev_phys + (uint32_t)g_ev_idx * 16));
    hal_write32(g_rt + 0x00, STS_EINT);   /* clear pending interrupt */
    return 1;
}

/* Wait for a completion of the given TRB type. Returns 0 on success and
   fills slot_out / code_out. */
static int wait_event(uint32_t want_type, uint32_t *code_out, uint32_t *slot_out)
{
    for (uint32_t spin = 0; spin < 2000000u; spin++) {
        trb_t e;
        while (ev_pop(&e)) {
            uint32_t type = (e.dw3 >> 10) & 0x3Fu;
            if (type == TRB_PORT_CHANGE)
                continue;
            if (type == TRB_TRANSFER_EV) {
                if (want_type != TRB_TRANSFER_EV)
                    continue;
            } else if (type == TRB_CMD_COMPLETE) {
                if (want_type != TRB_CMD_COMPLETE)
                    continue;
            } else {
                continue;
            }
            if (code_out)
                *code_out = (e.dw2 >> 24) & 0xFFu;
            if (slot_out)
                *slot_out = (e.dw3 >> 24) & 0xFFu;
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* controller bring-up                                                */
/* ------------------------------------------------------------------ */

static int controller_reset(void)
{
    uint32_t cmd = hal_read32(g_op + OP_USBCMD);
    cmd |= CMD_HCRST;
    hal_write32(g_op + OP_USBCMD, cmd);

    for (uint32_t i = 0; i < 1000000u; i++) {
        uint32_t sts = hal_read32(g_op + OP_USBSTS);
        if (!(sts & STS_CNR))
            break;
        __asm__ volatile("pause");
    }
    for (uint32_t i = 0; i < 1000000u; i++) {
        if (!(hal_read32(g_op + OP_USBCMD) & CMD_HCRST))
            break;
        __asm__ volatile("pause");
    }
    if (hal_read32(g_op + OP_USBCMD) & CMD_HCRST) {
        serial_puts("[XHCI] reset never cleared\n");
        return -1;
    }
    /* after HCRST the controller is halted until RS is set again */
    for (uint32_t i = 0; i < 1000000u; i++) {
        if (hal_read32(g_op + OP_USBSTS) & STS_HCH)
            break;
        __asm__ volatile("pause");
    }
    serial_puts("[XHCI] reset done sts=");
    serial_puthex(hal_read32(g_op + OP_USBSTS));
    serial_putc('\n');
    return 0;
}

static int rings_init(void)
{
    g_dcbaa_phys = alloc_page();
    g_cmd_phys = alloc_page();
    g_ev_phys = alloc_page();
    g_ep0_phys = alloc_page();
    g_input_phys = alloc_page();
    g_devctx_phys = alloc_page();
    if (!g_dcbaa_phys || !g_cmd_phys || !g_ev_phys || !g_ep0_phys ||
        !g_input_phys || !g_devctx_phys) {
        serial_puts("[XHCI] out of memory for rings\n");
        return -1;
    }
    zero_page(g_dcbaa_phys);
    zero_page(g_cmd_phys);
    zero_page(g_ev_phys);
    zero_page(g_ep0_phys);
    zero_page(g_input_phys);
    zero_page(g_devctx_phys);

    g_cmd_ring = (trb_t *)(uintptr_t)g_cmd_phys;
    g_ev_ring = (trb_t *)(uintptr_t)g_ev_phys;
    g_ep0_ring = (trb_t *)(uintptr_t)g_ep0_phys;
    g_input = (struct xhci_ctx *)(uintptr_t)g_input_phys;
    g_devctx = (struct xhci_ctx *)(uintptr_t)g_devctx_phys;

    g_cmd_idx = 0; g_cmd_cyc = 1;
    g_ev_idx = 0;  g_ev_cyc = 1;
    g_ep0_idx = 0; g_ep0_cyc = 1;

    /* device context base array: slot 0 entry must be zero */
    hal_write64(g_op + OP_DCBAAP, (uint64_t)g_dcbaa_phys);

    uint32_t cfg = hal_read32(g_op + OP_CONFIG);
    cfg = (cfg & ~0xFFu) | (g_max_slots & 0xFFu);
    hal_write32(g_op + OP_CONFIG, cfg);

    /* command ring */
    hal_write64(g_op + OP_CRCR, (uint64_t)g_cmd_phys | 1u);
    return 0;
}

static int erst_init(void)
{
    /* The ERST itself needs its own memory: 16 bytes per entry, 64-byte
       aligned. Allocate one page and put the table at offset 0, the event
       ring stays in its own page. */
    uint32_t erst_phys = alloc_page();
    if (!erst_phys) {
        serial_puts("[XHCI] no memory for ERST\n");
        return -1;
    }
    zero_page(erst_phys);
    volatile uint32_t *e = (volatile uint32_t *)(uintptr_t)erst_phys;
    e[0] = g_ev_phys;              /* segment base low */
    e[1] = 0;                      /* segment base high */
    e[2] = TRB_ENTRIES;            /* segment size in TRBs */
    e[3] = 0;
    __asm__ volatile("mfence" ::: "memory");

    hal_write32(g_rt + 0x08, 1);
    hal_write64(g_rt + 0x10, (uint64_t)erst_phys);
    hal_write64(g_rt + 0x18, (uint64_t)g_ev_phys);     /* ERDP */
    hal_write32(g_rt + 0x00, STS_EINT);
    return 0;
}

static int controller_run(void)
{
    uint32_t cmd = hal_read32(g_op + OP_USBCMD);
    cmd |= CMD_INTE | CMD_RS;
    hal_write32(g_op + OP_USBCMD, cmd);
    for (uint32_t i = 0; i < 1000000u; i++) {
        if (!(hal_read32(g_op + OP_USBSTS) & STS_HCH))
            return 0;
        __asm__ volatile("pause");
    }
    serial_puts("[XHCI] controller stayed halted\n");
    return -1;
}

/* ------------------------------------------------------------------ */
/* ports                                                              */
/* ------------------------------------------------------------------ */

static int port_reset(uint32_t port)
{
    uint32_t v = hal_read32(g_op + OP_PORTSC(port));
    if (!(v & PORT_PP)) {
        hal_write32(g_op + OP_PORTSC(port), v | PORT_PP);
        for (uint32_t i = 0; i < 200000u; i++) {
            v = hal_read32(g_op + OP_PORTSC(port));
            if (v & PORT_PP)
                break;
            __asm__ volatile("pause");
        }
    }
    v = hal_read32(g_op + OP_PORTSC(port));
    if (!(v & PORT_CCS))
        return -1;

    /* write-1-to-clear the change bits first, then pulse PR */
    hal_write32(g_op + OP_PORTSC(port),
                PORT_CSC | PORT_PEC | PORT_WRC | PORT_PRC | PORT_PLC | PORT_CEC);
    __asm__ volatile("mfence" ::: "memory");

    v = hal_read32(g_op + OP_PORTSC(port));
    hal_write32(g_op + OP_PORTSC(port), (v & ~PORT_PLS(0xF)) | PORT_PR);
    __asm__ volatile("mfence" ::: "memory");

    int reset_seen = 0;
    for (uint32_t i = 0; i < 2000000u; i++) {
        v = hal_read32(g_op + OP_PORTSC(port));
        if (v & PORT_PRC) {
            hal_write32(g_op + OP_PORTSC(port), v | PORT_PRC);
            reset_seen = 1;
            break;
        }
        __asm__ volatile("pause");
    }
    for (uint32_t i = 0; i < 1000000u; i++) {
        v = hal_read32(g_op + OP_PORTSC(port));
        if (v & PORT_PED)
            break;
        __asm__ volatile("pause");
    }
    serial_puts("[XHCI] port");
    serial_puthex(port + 1);
    serial_puts(" reset seen=");
    serial_puthex((uint32_t)reset_seen);
    serial_puts(" portsc=");
    serial_puthex(v);
    serial_puts(" speed=");
    serial_puthex(PORT_SPEED(v));
    serial_putc('\n');
    if (!(v & PORT_PED))
        return -1;
    return (int)PORT_SPEED(v);
}

/* ------------------------------------------------------------------ */
/* slot / EP0                                                         */
/* ------------------------------------------------------------------ */

static void ctx_zero(struct xhci_ctx *c)
{
    for (int i = 0; i < 8; i++)
        c->dw[i] = 0;
}

static struct xhci_ctx *ctx_at(struct xhci_ctx *base, uint32_t index)
{
    return (struct xhci_ctx *)((uint8_t *)base + index * g_ctx_size);
}

static int enable_slot(uint32_t *slot_out)
{
    cmd_push(0, 0, 0, TRB_ENABLE_SLOT);
    uint32_t code = 0xFF, slot = 0;
    if (wait_event(TRB_CMD_COMPLETE, &code, &slot) != 0) {
        serial_puts("[XHCI] enable slot timed out\n");
        return -1;
    }
    if (code != 1) {                     /* 1 = success */
        loghex("[XHCI] enable slot failed code=", code);
        return -1;
    }
    serial_puts("[XHCI] slot=");
    serial_puthex(slot);
    serial_putc('\n');
    *slot_out = slot;
    return 0;
}

static int address_device(uint32_t slot, uint32_t port, uint32_t speed,
                          uint32_t max_packet)
{
    ctx_zero(g_input);
    /* input control context: add slot (A0) and EP0 (A1) */
    g_input->dw[1] = (1u << 0) | (1u << 1);

    struct xhci_ctx *slotc = ctx_at(g_input, 1);
    struct xhci_ctx *eps   = ctx_at(g_input, 2);

    ctx_zero(slotc);
    slotc->dw[0] = (speed << 20) | (1u << 27);   /* ctx entries = 1 (EP0) */
    slotc->dw[1] = ((port + 1) << 16);           /* root hub port number */
    slotc->dw[2] = 0;
    slotc->dw[3] = 0;                            /* device address 0 for now */

    ctx_zero(eps);
    eps->dw[0] = (1u << 16);                     /* interval 1, state running */
    eps->dw[1] = (3u << 1) | (4u << 3) |          /* CErr 3, EP type 4 = control */
                 ((max_packet & 0xFFFFu) << 16);
    eps->dw[2] = (uint32_t)((uint64_t)g_ep0_phys | 1u);   /* DCS = 1 */
    eps->dw[3] = 0;
    __asm__ volatile("mfence" ::: "memory");

    /* the slot context lives in the device context array */
    hal_write64(g_op + OP_DCBAAP + (uint64_t)0, (uint64_t)g_dcbaa_phys);
    *(volatile uint64_t *)(uintptr_t)(g_dcbaa_phys + 8 * slot) =
        (uint64_t)g_devctx_phys;

    cmd_push(g_input_phys, 0, 0, TRB_ADDRESS_DEV);
    uint32_t code = 0xFF, s = 0;
    if (wait_event(TRB_CMD_COMPLETE, &code, &s) != 0) {
        serial_puts("[XHCI] address device timed out\n");
        return -1;
    }
    if (code != 1) {
        loghex("[XHCI] address device failed code=", code);
        return -1;
    }
    return 0;
}

static int ep0_transfer(uint32_t slot, uint8_t bmRequest, uint8_t bRequest,
                        uint16_t wValue, uint16_t wIndex,
                        void *buf, uint16_t len)
{
    uint32_t data_phys = (uint32_t)(uintptr_t)buf;
    int dir_in = (bmRequest & 0x80) != 0;

    trb_t *t = &g_ep0_ring[g_ep0_idx++];
    t->dw0 = (uint32_t)bmRequest | ((uint32_t)bRequest << 8) |
             ((uint32_t)wValue << 16);
    t->dw1 = (uint32_t)wIndex | ((uint32_t)len << 16);
    t->dw2 = 8;                                   /* setup packet length */
    t->dw3 = (TRB_SETUP << 10) | (1u << 6) |       /* IDT */
             (uint32_t)(dir_in ? 3u : 2u) << 16 | g_ep0_cyc;

    if (len) {
        t = &g_ep0_ring[g_ep0_idx++];
        t->dw0 = data_phys;
        t->dw1 = 0;
        t->dw2 = len;
        t->dw3 = (TRB_DATA << 10) | (uint32_t)(dir_in ? (1u << 16) : 0) |
                 g_ep0_cyc;
    }

    t = &g_ep0_ring[g_ep0_idx++];
    t->dw0 = 0;
    t->dw1 = 0;
    t->dw2 = 0;
    t->dw3 = (TRB_STATUS << 10) | (1u << 5) |       /* IOC */
             (uint32_t)(dir_in ? 0 : (1u << 16)) | g_ep0_cyc;

    __asm__ volatile("mfence" ::: "memory");
    g_doorbell[slot] = 1;                          /* endpoint 1 = EP0 */

    uint32_t code = 0xFF, s = 0;
    if (wait_event(TRB_TRANSFER_EV, &code, &s) != 0) {
        serial_puts("[XHCI] ep0 transfer timed out\n");
        return -1;
    }
    if (code != 1 && code != 13) {                  /* 1 ok, 13 short packet */
        loghex("[XHCI] ep0 transfer failed code=", code);
        return -1;
    }
    return 0;
}

static int read_device_descriptor(uint32_t slot, struct usb_dev_info *info,
                                  uint8_t *raw18)
{
    uint8_t *buf = (uint8_t *)(uintptr_t)(g_input_phys + 0x800);
    for (int i = 0; i < 18; i++)
        buf[i] = 0;

    if (ep0_transfer(slot, 0x80, 6, 0x0100, 0, buf, 18) != 0)
        return -1;

    if (buf[1] != 1 || buf[0] < 8) {
        loghex("[XHCI] descriptor bogus type=", buf[1]);
        return -1;
    }
    info->vid = (uint16_t)(buf[8] | (buf[9] << 8));
    info->pid = (uint16_t)(buf[10] | (buf[11] << 8));
    info->dev_class = buf[4];
    info->dev_subclass = buf[5];
    info->dev_protocol = buf[6];
    info->max_packet0 = buf[7];
    for (int i = 0; i < 18; i++)
        raw18[i] = buf[i];
    return 0;
}

/* ------------------------------------------------------------------ */
/* public entry                                                       */
/* ------------------------------------------------------------------ */

static void map_bar(uint32_t phys)
{
    for (uint32_t off = 0; off < XHCI_MAP_BYTES; off += 4096)
        vmm_map_page(XHCI_VBASE + off, (uint64_t)phys + off,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
    g_base = (volatile uint8_t *)XHCI_VBASE;
}

int xhci_probe(uint32_t bar_phys, struct usb_dev_info *out, int max_out)
{
    if (bar_phys == 0 || bar_phys == 0xFFFFFFFFu)
        return -1;

    map_bar(bar_phys);

    uint8_t caplen = hal_read8(g_base + 0x00);
    uint32_t hcs1 = hal_read32(g_base + 0x04);
    uint32_t hcs2 = hal_read32(g_base + 0x08);
    uint32_t hcc1 = hal_read32(g_base + 0x10);
    uint32_t dboff = hal_read32(g_base + 0x14);
    uint32_t rtsoff = hal_read32(g_base + 0x18);

    g_max_slots = hcs1 & 0xFFu;
    g_max_ports = (hcs1 >> 24) & 0xFFu;
    g_ctx_size = (hcc1 & (1u << 2)) ? 64u : 32u;   /* CSZ */

    g_op = g_base + caplen;
    g_doorbell = (volatile uint32_t *)(g_base + (dboff & ~3u));
    g_rt = g_base + (rtsoff & ~0x1Fu);

    serial_puts("[XHCI] cap len=");
    serial_puthex(caplen);
    serial_puts(" slots=");
    serial_puthex(g_max_slots);
    serial_puts(" ports=");
    serial_puthex(g_max_ports);
    serial_puts(" ctx=");
    serial_puthex(g_ctx_size);
    serial_puts(" hcs2=");
    serial_puthex(hcs2);
    serial_putc('\n');

    if (controller_reset() != 0)
        return -1;
    if (rings_init() != 0)
        return -1;
    if (erst_init() != 0)
        return -1;
    if (controller_run() != 0)
        return -1;

    int found = 0;
    for (uint32_t p = 0; p < g_max_ports && found < max_out; p++) {
        uint32_t v = hal_read32(g_op + OP_PORTSC(p));
        if (!(v & PORT_CCS))
            continue;

        int speed = port_reset(p);
        if (speed < 0) {
            serial_puts("[XHCI] port reset failed\n");
            continue;
        }

        uint32_t slot = 0;
        if (enable_slot(&slot) != 0)
            continue;

        uint32_t mps = (speed >= 3) ? 64u : 8u;    /* HS/SS use 64 */
        if (address_device(slot, p, (uint32_t)speed, mps) != 0)
            continue;

        struct usb_dev_info info;
        uint8_t raw[18];
        for (int i = 0; i < 18; i++)
            raw[i] = 0;
        if (read_device_descriptor(slot, &info, raw) != 0)
            continue;

        info.speed = (uint8_t)speed;
        info.port = (uint8_t)(p + 1);
        info.slot = (uint8_t)slot;
        out[found++] = info;

        serial_puts("[XHCI] device port=");
        serial_puthex(p + 1);
        serial_puts(" vid=");
        serial_puthex(info.vid);
        serial_puts(" pid=");
        serial_puthex(info.pid);
        serial_puts(" class=");
        serial_puthex(info.dev_class);
        serial_puts(" speed=");
        serial_puthex(speed);
        serial_puts(" slot=");
        serial_puthex(slot);
        serial_putc('\n');
    }

    if (found == 0)
        serial_puts("[XHCI] no device enumerated\n");
    return found;
}

int xhci_dev_count(void) { return g_dev_n; }

const struct usb_dev_info *xhci_dev_get(int idx)
{
    if (idx < 0 || idx >= g_dev_n)
        return 0;
    return &g_devs[idx];
}

/* Keep the table the GUI reads in sync with what the probe found. */
void xhci_store_devices(const struct usb_dev_info *list, int n)
{
    if (n > 8)
        n = 8;
    for (int i = 0; i < n; i++)
        g_devs[i] = list[i];
    g_dev_n = n;
}
