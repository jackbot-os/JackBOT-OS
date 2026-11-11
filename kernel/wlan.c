#include <stdint.h>
#include <stddef.h>
#include <kernel/wlan.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>
#include <kernel/pci.h>
#include "../../drivers/usb/xhci.h"

#define WLAN_CFG_MAX 512

static struct wlan_dev g_devs[WLAN_MAX_DEV];
static int g_dev_n;

static const struct wlan_driver *g_drv[] = {
    &wlan_rtl8xxxu,
    &wlan_rt2800,
    &wlan_ath9k_htc,
    &wlan_mt7601u,
};
#define N_DRV (int)(sizeof(g_drv) / sizeof(g_drv[0]))

static void scpy(char *dst, const char *src, int cap)
{
    int i = 0;
    while (src && src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ---------------- transport ------------------------------------------- */

int wlan_ctrl_in(uint8_t slot, uint8_t req, uint16_t val, uint16_t idx,
                 void *buf, uint16_t len)
{
    unsigned char *scratch = (unsigned char *)xhci_scratch();
    if (!scratch || len > xhci_scratch_size())
        return -1;
    if (xhci_ctrl_in(slot, req, val, idx, scratch, len) != 0)
        return -1;
    memcpy(buf, scratch, len);
    return 0;
}

int wlan_ctrl_out(uint8_t slot, uint8_t req, uint16_t val, uint16_t idx,
                  const void *buf, uint16_t len)
{
    unsigned char *scratch = (unsigned char *)xhci_scratch();
    if (!scratch || len > xhci_scratch_size())
        return -1;
    memcpy(scratch, buf, len);
    return xhci_ctrl_out(slot, req, val, idx, scratch, len);
}

int wlan_read_desc(uint8_t slot, uint8_t type, uint8_t index, void *buf,
                   uint16_t len)
{
    return wlan_ctrl_in(slot, 6, (uint16_t)((type << 8) | index), 0, buf, len);
}

/* ---------------- descriptors ----------------------------------------- */

/* Copies a UTF-16LE USB string descriptor into plain ASCII, dropping the
   high byte of every code unit and the language tag wrapper. */
static void utf16_to_ascii(const unsigned char *d, int bytes, char *out, int cap)
{
    int n = 0;
    if (bytes < 2 || d[1] != 3)
        return;
    for (int i = 2; i + 1 < bytes && n < cap - 1; i += 2) {
        unsigned char lo = d[i];
        if (lo >= 32 && lo < 127)
            out[n++] = (char)lo;
    }
    out[n] = 0;
}

static int read_string(uint8_t slot, uint8_t index, char *out, int cap)
{
    unsigned char raw[128];
    int got = 0;
    out[0] = 0;
    if (!index)
        return -1;
    if (wlan_read_desc(slot, 3, index, raw, 4) != 0)
        return -1;
    got = raw[0];
    if (got < 2 || got > (int)sizeof(raw))
        return -1;
    if (wlan_read_desc(slot, 3, index, raw, (uint16_t)got) != 0)
        return -1;
    utf16_to_ascii(raw, got, out, cap);
    return out[0] ? 0 : -1;
}

int wlan_cfg_parse(struct wlan_dev *d)
{
    unsigned char cfg[WLAN_CFG_MAX];
    int total, off;

    d->n_eps = 0;
    d->if_class = d->if_subclass = d->if_protocol = 0;
    d->ep_in = d->ep_out = d->ep_irq = 0;
    if (wlan_read_desc(d->slot, 2, 0, cfg, 9) != 0)
        return -1;
    if (cfg[1] != 2 || cfg[0] < 9)
        return -1;
    total = (int)(cfg[2] | (cfg[3] << 8));
    if (total < 9 || total > WLAN_CFG_MAX)
        return -1;
    if (wlan_read_desc(d->slot, 2, 0, cfg, (uint16_t)total) != 0)
        return -1;

    off = cfg[0];
    while (off + 2 <= total) {
        unsigned char len = cfg[off];
        unsigned char type = cfg[off + 1];
        if (len < 2 || off + len > total)
            break;
        if (type == 4 && len >= 9 && !d->if_class) {
            d->if_class = cfg[off + 5];
            d->if_subclass = cfg[off + 6];
            d->if_protocol = cfg[off + 7];
            d->n_eps = cfg[off + 4];
        } else if (type == 5 && len >= 7) {
            unsigned char attr = cfg[off + 3];
            unsigned char num = cfg[off + 2];
            uint16_t maxp = (uint16_t)(cfg[off + 4] | (cfg[off + 5] << 8));
            int is_in = (cfg[off + 2] & 0x80) != 0;
            if ((attr & 3) == 2 && is_in) {
                if (!d->ep_in) { d->ep_in = num & 0x0F; d->ep_in_maxpkt = maxp > 255 ? 255 : (uint8_t)maxp; }
            } else if ((attr & 3) == 2 && !is_in) {
                if (!d->ep_out) { d->ep_out = num & 0x0F; d->ep_out_maxpkt = maxp > 255 ? 255 : (uint8_t)maxp; }
            } else if ((attr & 3) == 3) {
                if (!d->ep_irq) d->ep_irq = num & 0x0F;
            }
        }
        off += len;
    }
    return 0;
}

/* ---------------- scan ------------------------------------------------- */

static const char *family_for_vid(uint16_t vid)
{
    switch (vid) {
    case 0x0BDA: return "Realtek USB";
    case 0x148F: case 0x1E0A: case 0x0411: return "Ralink USB";
    case 0x0CF3: case 0x0D8E: return "Atheros USB";
    case 0x0E8D: return "MediaTek USB";
    case 0x8087: return "Intel USB";
    case 0x0A5C: case 0x04F2: case 0x13D3: case 0x050D: return "Broadcom USB";
    default: return "unknown USB vendor";
    }
}

static void parse_serial_mac(const char *s, struct wlan_dev *d)
{
    unsigned char m[6];
    int i;
    if (strlen(s) != 12)
        return;
    for (i = 0; i < 12; i++) {
        int v;
        char c = s[i];
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return;
        if ((i & 1) == 0) m[i / 2] = (unsigned char)(v << 4);
        else m[i / 2] |= (unsigned char)v;
    }
    for (i = 0; i < 6; i++)
        d->mac[i] = m[i];
    d->mac_valid = 1;
}

static int probe_one_usb(const struct usb_dev_info *u, struct wlan_dev *d)
{
    unsigned char raw[18];
    char tmp[64];

    memset(d, 0, sizeof(*d));
    d->vid = u->vid;
    d->pid = u->pid;
    d->slot = u->slot;
    d->port = u->port;
    d->speed = u->speed;
    d->family = family_for_vid(u->vid);
    d->driver = "none";
    scpy(d->state, "identified", WLAN_STR_MAX);
    d->product[0] = 0;
    d->serial[0] = 0;

    if (wlan_read_desc(u->slot, 1, 0, raw, 18) == 0) {
        if (read_string(u->slot, raw[14], d->product, WLAN_STR_MAX) == 0) { }
        if (read_string(u->slot, raw[15], d->serial, WLAN_STR_MAX) == 0) { }
        parse_serial_mac(d->serial, d);
    }
    if (!d->product[0]) {
        /* Some dongles carry the model in the interface string (index 2). */
        read_string(u->slot, 2, tmp, sizeof(tmp));
        scpy(d->product, tmp, WLAN_STR_MAX);
    }

    wlan_cfg_parse(d);

    /* Wireless USB adapters present themselves either as a vendor-specific
       interface (0xFF) under a class-0 device, or with the vendor class on
       the device itself. Everything else on the bus (storage, HID, audio)
       is not ours and must not show up in the adapter list. */
    if (d->if_class != 0xFF && d->if_class != 0x00)
        return 0;

    for (int i = 0; i < N_DRV; i++) {
        const struct wlan_driver *dr = g_drv[i];
        if (!dr->match(u->vid, u->pid))
            continue;
        d->driver = dr->name;
        d->fw_required = dr->fw_required;
        if (dr->probe && dr->probe(d) == 0) {
            if (!d->reg_ok)
                scpy(d->state, "identified", WLAN_STR_MAX);
        }
        break;
    }
    return 1;
}

int wlan_register(const struct wlan_driver *d)
{
    (void)d;
    return 0;   /* drivers are linked in through g_drv at build time */
}

int wlan_scan(void)
{
    int n = xhci_dev_count();

    g_dev_n = 0;
    for (int i = 0; i < n && g_dev_n < WLAN_MAX_DEV; i++) {
        const struct usb_dev_info *u = xhci_dev_get(i);
        struct wlan_dev tmp;
        if (!u || !u->slot)
            continue;
        if (probe_one_usb(u, &tmp) != 0)
            continue;                  /* not a wireless interface */
        g_devs[g_dev_n++] = tmp;
    }

    /* PCI wireless adapters are listed too: the kernel can name them but has
       no MAC80211 layer for them yet, so they stay "needs firmware". */
    for (int i = 0; i < pci_count() && g_dev_n < WLAN_MAX_DEV; i++) {
        struct pci_dev *p = pci_get(i);
        if (!p || p->class_code != 0x02 || p->subclass != 0x80)
            continue;
        struct wlan_dev *d = &g_devs[g_dev_n++];
        memset(d, 0, sizeof(*d));
        d->vid = p->vendor;
        d->pid = p->device;
        d->family = "PCI wireless";
        d->driver = p->vendor == 0x8086 ? "iwm/iwlwifi (not linked)" : "none";
        d->fw_required = 1;
        scpy(d->state, "no MAC80211 layer in this kernel", WLAN_STR_MAX);
        scpy(d->product, "PCI 0x02/0x80 adapter", WLAN_STR_MAX);
    }

    serial_puts("[WLAN] devices=");
    serial_puthex((uint32_t)g_dev_n);
    serial_putc('\n');
    for (int i = 0; i < g_dev_n; i++) {
        serial_puts("[WLAN] ");
        serial_puts(g_devs[i].driver);
        serial_puts(" vid=");
        serial_puthex(g_devs[i].vid);
        serial_puts(" pid=");
        serial_puthex(g_devs[i].pid);
        serial_puts(" product=");
        serial_puts(g_devs[i].product[0] ? g_devs[i].product : "-");
        serial_putc('\n');
    }
    return g_dev_n;
}

int wlan_count(void) { return g_dev_n; }

const struct wlan_dev *wlan_dev_get(int idx)
{
    if (idx < 0 || idx >= g_dev_n)
        return 0;
    return &g_devs[idx];
}
