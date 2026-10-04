#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>

#define FB_VBASE 0xffff800080000000ull
#define ADDR_MASK 0x000ffffffffff000ull

#define PCI_CONFIG_ADDR 0xcf8
#define PCI_CONFIG_DATA 0xcfc
#define VBE_DISPI_IOPORT_INDEX 0x1ce
#define VBE_DISPI_IOPORT_DATA 0x1cf
#define VBE_DISPI_INDEX_ID 0
#define VBE_DISPI_INDEX_XRES 1
#define VBE_DISPI_INDEX_YRES 2
#define VBE_DISPI_INDEX_BPP 3
#define VBE_DISPI_INDEX_ENABLE 4
#define VBE_DISPI_LFB_ENABLED 0x40
#define VBE_DISPI_ENABLED 0x01

#define BOCHS_VENDOR 0x1234
#define BOCHS_DEVICE 0x1111

static struct {
    int present;
    uint32_t width, height, pitch;
    uint8_t bpp, bytes_pp;
    uint8_t rpos, rsize, gpos, gsize, bpos, bsize;
    uint8_t *addr;
} g_fb;

static uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outl(uint16_t port, uint32_t v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outw(uint16_t port, uint16_t v)
{
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11) | ((uint32_t)func << 8)
                  | (reg & 0xfc);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg,
                      uint32_t val)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16)
                  | ((uint32_t)dev << 11) | ((uint32_t)func << 8)
                  | (reg & 0xfc);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static void vbe_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint32_t mask_val(uint8_t size)
{
    if (size == 0)
        return 0;
    if (size >= 32)
        return 0xffffffffu;
    return (1u << size) - 1;
}

int fb_bochs_init(uint32_t width, uint32_t height)
{
    int found = 0;
    uint8_t bus = 0, dev = 0, func = 0;
    for (uint8_t d = 0; d < 32; d++) {
        uint32_t vendor = pci_read(0, d, 0, 0) & 0xffff;
        uint32_t device = (pci_read(0, d, 0, 0) >> 16) & 0xffff;
        if (vendor == BOCHS_VENDOR && device == BOCHS_DEVICE) {
            found = 1;
            dev = d;
            break;
        }
    }
    if (!found) {
        serial_puts("bochs VGA not found\n");
        return 0;
    }

    uint32_t cmd = pci_read(bus, dev, func, 4);
    cmd |= (1u << 1) | (1u << 2);
    pci_write(bus, dev, func, 4, cmd);

    uint32_t bar = pci_read(bus, dev, func, 0x10);
    uint64_t fbphys = bar & ~0xfull;

    vbe_write(VBE_DISPI_INDEX_ENABLE, 0);
    vbe_write(VBE_DISPI_INDEX_ID, 0xb0c0);
    vbe_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    vbe_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    vbe_write(VBE_DISPI_INDEX_BPP, 32);
    vbe_write(VBE_DISPI_INDEX_ENABLE,
              VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    g_fb.width = width;
    g_fb.height = height;
    g_fb.pitch = width * 4;
    g_fb.bpp = 32;
    g_fb.bytes_pp = 4;
    g_fb.rpos = 16; g_fb.rsize = 8;
    g_fb.gpos = 8;  g_fb.gsize = 8;
    g_fb.bpos = 0;  g_fb.bsize = 8;

    uint64_t size = (uint64_t)g_fb.pitch * height;
    uint64_t pages = (size + 4095) / 4096;
    for (uint64_t p = 0; p < pages; p++)
        vmm_map_page(FB_VBASE + p * 4096, fbphys + p * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PWT);
    g_fb.addr = (uint8_t *)FB_VBASE;
    g_fb.present = 1;

    serial_puts("bochs framebuffer ");
    serial_puthex(width); serial_putc('x'); serial_puthex(height);
    serial_puts(" bar="); serial_puthex(bar);
    serial_puts(" fbphys="); serial_puthex(fbphys);
    serial_putc('\n');
    (void)inw;
    return 1;
}

int fb_present(void) { return g_fb.present; }
uint32_t fb_width(void) { return g_fb.width; }
uint32_t fb_height(void) { return g_fb.height; }
uint32_t fb_pitch(void) { return g_fb.pitch; }
uint8_t fb_bpp(void) { return g_fb.bpp; }

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t out = 0;
    out |= ((uint32_t)r & mask_val(g_fb.rsize)) << g_fb.rpos;
    out |= ((uint32_t)g & mask_val(g_fb.gsize)) << g_fb.gpos;
    out |= ((uint32_t)b & mask_val(g_fb.bsize)) << g_fb.bpos;
    return out;
}

void fb_putpixel(int x, int y, uint32_t packed)
{
    if (!g_fb.present || x < 0 || y < 0
        || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height)
        return;
    uint8_t *p = g_fb.addr + (uint64_t)y * g_fb.pitch + x * g_fb.bytes_pp;
    if (g_fb.bytes_pp == 4)
        *(uint32_t *)p = packed;
    else
        for (int i = 0; i < 3; i++)
            p[i] = (packed >> (i * 8)) & 0xff;
}

uint32_t fb_getpixel(int x, int y)
{
    if (!g_fb.present || x < 0 || y < 0
        || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height)
        return 0;
    uint8_t *p = g_fb.addr + (uint64_t)y * g_fb.pitch + x * g_fb.bytes_pp;
    if (g_fb.bytes_pp == 4)
        return *(uint32_t *)p;
    return fb_rgb(p[2], p[1], p[0]);
}

void fb_clear(uint32_t packed)
{
    if (!g_fb.present)
        return;
    for (uint32_t y = 0; y < g_fb.height; y++) {
        uint8_t *row = g_fb.addr + (uint64_t)y * g_fb.pitch;
        if (g_fb.bytes_pp == 4) {
            uint32_t *p = (uint32_t *)row;
            for (uint32_t x = 0; x < g_fb.width; x++)
                p[x] = packed;
        }
    }
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t packed)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_putpixel(x + i, y + j, packed);
}

void fb_blit(int dx, int dy, int w, int h, const uint32_t *pixels)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_putpixel(dx + i, dy + j, pixels[j * w + i]);
}
