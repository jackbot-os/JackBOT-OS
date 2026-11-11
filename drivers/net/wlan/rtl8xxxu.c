/*
 * Realtek USB WLAN family driver (rtl8xxxu-class chips: 8188EU/CUS,
 * 8192CU/EU, 8812AU, 8821AU ...).
 *
 * What is implemented here: the vendor register transport every Realtek USB
 * part speaks (control IN, bRequest 0x05, wValue 0, wIndex = register, 4 bytes
 * little endian). It is used to read chip register 0 and prove the device is
 * alive and answering. MAC readout, firmware, channel tuning and the 802.11
 * state machine are NOT here - this kernel has no MAC80211 layer yet.
 */
#include <stdint.h>
#include <kernel/wlan.h>
#include <kernel/kstring.h>

static int rtl_match(uint16_t vid, uint16_t pid)
{
    (void)pid;
    return vid == 0x0BDA;      /* Realtek Semiconductor */
}

static int rtl_read32(struct wlan_dev *d, uint32_t addr, uint32_t *out)
{
    uint8_t b[4];
    if (wlan_ctrl_in(d->slot, 0x05, 0, (uint16_t)addr, b, 4) != 0)
        return -1;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static int rtl_probe(struct wlan_dev *d)
{
    uint32_t v = 0;

    if (rtl_read32(d, 0x0000, &v) == 0) {
        d->reg_ok = 1;
        d->reg0 = v;
        strcpy(d->state, "register-ok, needs 802.11 stack");
        return 0;
    }
    strcpy(d->state, "identified, register read failed");
    return -1;
}

const struct wlan_driver wlan_rtl8xxxu = {
    "rtl8xxxu", "Realtek USB", 0, rtl_match, rtl_probe
};
