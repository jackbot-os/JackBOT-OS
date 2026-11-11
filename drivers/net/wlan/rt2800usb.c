/*
 * Ralink RT2x00 USB family driver (RT2870/RT3070/RT5370/RT5572 ...).
 * Device identity and endpoint set are real; register access and the
 * rt2x00 MAC/BBP/RF programming sequence are not implemented yet.
 */
#include <stdint.h>
#include <kernel/wlan.h>
#include <kernel/kstring.h>

static int rt_match(uint16_t vid, uint16_t pid)
{
    (void)pid;
    return vid == 0x148F || vid == 0x1E0A || vid == 0x0411;
}

static int rt_probe(struct wlan_dev *d)
{
    strcpy(d->state, "identified, needs rt2x00 802.11 stack");
    return 0;
}

const struct wlan_driver wlan_rt2800 = {
    "rt2800usb", "Ralink USB", 1, rt_match, rt_probe
};
