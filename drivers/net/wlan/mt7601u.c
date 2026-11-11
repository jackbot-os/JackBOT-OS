/*
 * MediaTek MT7601U USB WLAN driver.
 * The part needs mt7601u.bin uploaded before the MAC answers; without a
 * firmware loader only identification is possible.
 */
#include <stdint.h>
#include <kernel/wlan.h>
#include <kernel/kstring.h>

static int mt_match(uint16_t vid, uint16_t pid)
{
    (void)pid;
    return vid == 0x0E8D;
}

static int mt_probe(struct wlan_dev *d)
{
    strcpy(d->state, "needs mt7601u.bin (no firmware loader)");
    return 0;
}

const struct wlan_driver wlan_mt7601u = {
    "mt7601u", "MediaTek USB", 1, mt_match, mt_probe
};
