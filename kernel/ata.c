#include <stdint.h>
#include <stddef.h>
#include <kernel/ata.h>
#include <kernel/serial.h>

#define BASE 0x1F0
#define REG_DATA 0x1F0
#define REG_SECCNT 0x1F2
#define REG_LBALOW 0x1F3
#define REG_LBAMID 0x1F4
#define REG_LBAHIGH 0x1F5
#define REG_DRIVE 0x1F6
#define REG_STATUS 0x1F7
#define REG_CMD 0x1F7
#define REG_ALTSTATUS 0x3F6

#define ST_BSY 0x80
#define ST_DRDY 0x40
#define ST_DF 0x20
#define ST_DRQ 0x08
#define ST_ERR 0x01

#define CMD_IDENTIFY 0xEC
#define CMD_READ_EXT 0x24
#define CMD_WRITE_EXT 0x34
#define CMD_FLUSH 0xE7

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void insw(uint16_t port, void *buf, uint32_t cnt)
{
    __asm__ volatile("rep insw" : "+D"(buf), "+c"(cnt) : "d"(port) : "memory");
}
static inline void outsw(uint16_t port, const void *buf, uint32_t cnt)
{
    __asm__ volatile("rep outsw" : "+S"(buf), "+c"(cnt) : "d"(port) : "memory");
}

static int present;
static uint64_t total_sectors;

static uint8_t wait_not_bsy(void)
{
    uint8_t s;
    for (uint32_t i = 0; i < 100000; i++) {
        s = inb(REG_STATUS);
        if (!(s & ST_BSY))
            return s;
    }
    return inb(REG_STATUS);
}

static int wait_drq(void)
{
    for (uint32_t i = 0; i < 100000; i++) {
        uint8_t s = inb(REG_STATUS);
        if ((s & ST_ERR) || (s & ST_DF))
            return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ))
            return 0;
    }
    return -1;
}

static void select_master(void)
{
    outb(REG_DRIVE, 0xA0);
    for (uint32_t i = 0; i < 1000; i++)
        inb(REG_ALTSTATUS);
}

static int ide_controller_present(void)
{
    int ff = 0;
    for (uint32_t i = 0; i < 64; i++) {
        uint8_t s = inb(REG_STATUS);
        if (s == 0xFF)
            ff++;
        inb(REG_ALTSTATUS);
    }
    return ff < 64;
}

int ata_init(void)
{
    uint16_t ident[256];

    select_master();

    if (!ide_controller_present()) {
        serial_puts("[ATA] no IDE controller (AHCI-only), skip PIO\n");
        return -1;
    }

    outb(REG_SECCNT, 0);
    outb(REG_LBALOW, 0);
    outb(REG_LBAMID, 0);
    outb(REG_LBAHIGH, 0);
    outb(REG_CMD, CMD_IDENTIFY);

    uint8_t s = wait_not_bsy();
    if (s == 0) {
        serial_puts("[ATA] no drive\n");
        return -1;
    }
    if (wait_drq() < 0) {
        serial_puts("[ATA] identify failed\n");
        return -1;
    }
    insw(REG_DATA, ident, 256);

    present = 1;
    total_sectors = (uint64_t)ident[60] | ((uint64_t)ident[61] << 16);
    serial_puts("[ATA] sectors=");
    serial_puthex(total_sectors);
    serial_putc('\n');
    return 0;
}

int ata_present(void)
{
    return present;
}

uint64_t ata_sector_count(void)
{
    return total_sectors;
}

static void lba48_setup(uint64_t lba, uint8_t count, uint8_t cmd)
{
    outb(REG_SECCNT, (count >> 8) & 0xFF);
    outb(REG_LBALOW, (lba >> 24) & 0xFF);
    outb(REG_LBAMID, (lba >> 32) & 0xFF);
    outb(REG_LBAHIGH, (lba >> 40) & 0xFF);
    outb(REG_SECCNT, count & 0xFF);
    outb(REG_LBALOW, lba & 0xFF);
    outb(REG_LBAMID, (lba >> 8) & 0xFF);
    outb(REG_LBAHIGH, (lba >> 16) & 0xFF);
    outb(REG_DRIVE, 0x40);
    outb(REG_CMD, cmd);
}

int ata_read(uint64_t lba, uint8_t count, void *buf)
{
    if (!present)
        return -1;
    uint8_t *p = buf;
    lba48_setup(lba, count, CMD_READ_EXT);
    for (uint8_t i = 0; i < count; i++) {
        if (wait_drq() < 0)
            return -1;
        insw(REG_DATA, p, 256);
        p += 512;
    }
    wait_not_bsy();
    return 0;
}

int ata_write(uint64_t lba, uint8_t count, const void *buf)
{
    if (!present)
        return -1;
    const uint8_t *p = buf;
    lba48_setup(lba, count, CMD_WRITE_EXT);
    for (uint8_t i = 0; i < count; i++) {
        if (wait_drq() < 0)
            return -1;
        outsw(REG_DATA, p, 256);
        p += 512;
    }
    wait_not_bsy();
    outb(REG_CMD, CMD_FLUSH);
    wait_not_bsy();
    return 0;
}

int ata_flush_cache(void)
{
    if (!present)
        return -1;
    select_master();
    outb(REG_CMD, CMD_FLUSH);
    uint8_t s = wait_not_bsy();
    return (s & (ST_ERR | ST_DF)) ? -1 : 0;
}
