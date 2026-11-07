#include <kernel/blk.h>
#include <kernel/ata.h>
#include <kernel/ahci.h>
#include <kernel/serial.h>

#define BLK_NONE 0
#define BLK_ATA  1
#define BLK_AHCI 2

static int g_kind = BLK_NONE;
static int g_port = -1;
static uint64_t g_sectors;

int blk_init(void)
{
    g_kind = BLK_NONE;
    g_port = -1;
    g_sectors = 0;

    if (ata_present()) {
        g_kind = BLK_ATA;
        g_sectors = ata_sector_count();
    } else if (ahci_present()) {
        int p = ahci_primary_port();
        if (p >= 0) {
            uint64_t n = 0;
            if (ahci_port_sectors(p, &n) == 0 && n > 0) {
                g_kind = BLK_AHCI;
                g_port = p;
                g_sectors = n;
            }
        }
    }

    serial_puts("[BLK] backend=");
    serial_puts(blk_backend_name());
    serial_puts(" sectors=");
    serial_puthex((uint32_t)g_sectors);
    serial_putc('\n');
    return g_kind != BLK_NONE ? 0 : -1;
}

int blk_present(void) { return g_kind != BLK_NONE; }
uint64_t blk_sectors(void) { return g_sectors; }
int blk_port(void) { return g_port; }

const char *blk_backend_name(void)
{
    if (g_kind == BLK_ATA)
        return "ATA";
    if (g_kind == BLK_AHCI)
        return "AHCI";
    return "none";
}

int blk_read(uint64_t lba, uint32_t count, void *buf)
{
    if (g_kind == BLK_ATA)
        return ata_read(lba, (uint8_t)count, buf);
    if (g_kind == BLK_AHCI)
        return ahci_read_sector(g_port, lba, count, buf);
    return -1;
}

int blk_write(uint64_t lba, uint32_t count, const void *buf)
{
    if (g_kind == BLK_ATA)
        return ata_write(lba, (uint8_t)count, buf);
    if (g_kind == BLK_AHCI)
        return ahci_write_sector(g_port, lba, count, buf);
    return -1;
}

int blk_flush(void)
{
    if (g_kind == BLK_ATA)
        return ata_flush_cache();
    if (g_kind == BLK_AHCI)
        return ahci_flush(g_port);
    return -1;
}

int blk_read_one(uint64_t lba, uint8_t *out512)
{
    return blk_read(lba, 1, out512) == 0 ? 0 : -1;
}
