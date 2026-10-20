#include <stdint.h>
#include <stddef.h>
#include <kernel/grub_install.h>
#include <kernel/part.h>
#include <kernel/ata.h>
#include <kernel/serial.h>

static uint8_t last_sec[512];

int grub_write_core(uint32_t lba, const void *core, uint32_t bytes)
{
    if (!ata_present() || !core || bytes == 0)
        return -1;

    uint32_t total_sectors = (bytes + 511) / 512;
    if ((uint64_t)lba + total_sectors >
        (uint64_t)GRUB_CORE_LBA + GRUB_EMBED_SECTORS) {
        serial_puts("[GRUB] core too big for embed area\n");
        return -1;
    }

    const uint8_t *p = (const uint8_t *)core;
    uint32_t cur = lba;
    uint32_t full = bytes / 512;
    uint32_t rem = bytes % 512;

    uint32_t done = 0;
    while (done < full) {
        uint32_t chunk = full - done;
        if (chunk > 128)
            chunk = 128;
        if (ata_write(cur, (uint8_t)chunk, p + done * 512) != 0)
            return -1;
        cur += chunk;
        done += chunk;
    }

    if (rem) {
        for (uint32_t i = 0; i < 512; i++)
            last_sec[i] = (i < rem) ? p[full * 512 + i] : 0;
        if (ata_write(cur, 1, last_sec) != 0)
            return -1;
    }

    if (ata_flush_cache() != 0)
        return -1;
    serial_puts("[GRUB] core written sectors=");
    serial_puthex(total_sectors);
    serial_putc('\n');
    return 0;
}

int grub_install_boot_disk(const uint8_t *bootimg,
                           const void *core, uint32_t core_bytes,
                           const struct part_layout *pl)
{
    if (!bootimg || !core || !pl)
        return -1;
    if (grub_write_core(GRUB_CORE_LBA, core, core_bytes) != 0)
        return -1;
    if (part_write_mbr(bootimg, pl) != 0)
        return -1;
    serial_puts("[GRUB] MBR + core installed\n");
    return 0;
}
