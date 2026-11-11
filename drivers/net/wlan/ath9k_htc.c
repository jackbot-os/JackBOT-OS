/*
 * Atheros AR9271 / AR7010 USB family driver (ath9k_htc).
 * These parts are firmware driven: the host must load htc_9271.fw through the
 * firmware download endpoint before the chip answers anything. This kernel has
 * no firmware loader yet, so the driver only identifies the device and records
 * which firmware it would need.
 */
#include <stdint.h>
#include <kernel/wlan.h>
#include <kernel/kstring.h>

static int ath_match(uint16_t vid, uint16_t pid)
{
    (void)pid;
    return vid == 0x0CF3 || vid == 0x0D8E;
}

static int ath_probe(struct wlan_dev *d)
{
    strcpy(d->state, "needs htc_9271.fw (no firmware loader)");
    return 0;
}

const struct wlan_driver wlan_ath9k_htc = {
    "ath9k_htc", "Atheros USB", 1, ath_match, ath_probe
};
