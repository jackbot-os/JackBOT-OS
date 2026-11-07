#include <stdint.h>
#include <stddef.h>
#include <kernel/fb.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>
#include <kernel/heap.h>
#include <kernel/svga.h>

#define FB_VBASE 0xffff800080000000ull
#define ADDR_MASK 0x000ffffffffff000ull

/* Highest mode this build accepts. Anything the firmware/bootloader reports
   beyond these bounds is clamped here so a bogus huge framebuffer cannot blow
   the kernel heap or over-map the FB window. 4K = 3840x2160. */
#define FB_MAX_W     3840u
#define FB_MAX_H     2160u
#define FB_MAX_PIX   ((uint64_t)FB_MAX_W * FB_MAX_H)

static void fb_clamp_mode(uint32_t *w, uint32_t *h)
{
    if (*w > FB_MAX_W)
        *w = FB_MAX_W;
    if (*h > FB_MAX_H)
        *h = FB_MAX_H;
    if ((uint64_t)*w * (uint64_t)*h > FB_MAX_PIX) {
        *h = FB_MAX_H;
        *w = FB_MAX_W;
    }
}

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
#define VBOX_VENDOR 0x80ee
#define VBOX_DEVICE 0xbeef
#define VMSVGA_VENDOR 0x15ad
#define VMSVGA_DEVICE 0x0405

/* VMware SVGA II register ports */
#define SVGA_INDEX_PORT 0x5650
#define SVGA_VALUE_PORT 0x5654
#define SVGA_REG_ID 0
#define SVGA_REG_ENABLE 1
#define SVGA_REG_WIDTH 3
#define SVGA_REG_HEIGHT 4
#define SVGA_REG_BITS_PER_PIXEL 5
#define SVGA_REG_FB_START 10
#define SVGA_ENABLE_ENABLED 0x0001

static struct {
    int present;
    int gpu;
    uint32_t width, height, pitch;
    uint8_t bpp, bytes_pp;
    uint8_t rpos, rsize, gpos, gsize, bpos, bsize;
    uint8_t *addr;
} g_fb;

static uint32_t *g_back;

/* Up to DIRTY_MAX independent dirty rectangles. A single merged rectangle was
   what made the whole desktop flicker: the pointer, an icon and the taskbar
   clock sit far apart, so their bounding box covered almost the entire screen
   and every frame copied it to the scanout. */
#define DIRTY_MAX 12
struct dirty_rect { int x0, y0, x1, y1; };
static struct dirty_rect g_rects[DIRTY_MAX];
static int g_nr;
static int g_last = -1;

static void dirty_add(int x0, int y0, int x1, int y1)
{
    if (g_nr > 0 && g_last >= 0) {
        struct dirty_rect *r = &g_rects[g_last];
        if (x0 >= r->x0 && y0 >= r->y0 && x1 <= r->x1 && y1 <= r->y1)
            return;
    }
    for (int i = 0; i < g_nr; i++) {
        struct dirty_rect *r = &g_rects[i];
        if (x0 <= r->x1 && x1 >= r->x0 && y0 <= r->y1 && y1 >= r->y0) {
            if (x0 < r->x0) r->x0 = x0;
            if (y0 < r->y0) r->y0 = y0;
            if (x1 > r->x1) r->x1 = x1;
            if (y1 > r->y1) r->y1 = y1;
            g_last = i;
            return;
        }
    }
    if (g_nr < DIRTY_MAX) {
        g_rects[g_nr].x0 = x0; g_rects[g_nr].y0 = y0;
        g_rects[g_nr].x1 = x1; g_rects[g_nr].y1 = y1;
        g_last = g_nr;
        g_nr++;
        return;
    }
    /* Full: fold this one into the first slot. Costs a bigger copy but keeps
       the screen correct. */
    struct dirty_rect *r = &g_rects[0];
    if (x0 < r->x0) r->x0 = x0;
    if (y0 < r->y0) r->y0 = y0;
    if (x1 > r->x1) r->x1 = x1;
    if (y1 > r->y1) r->y1 = y1;
    g_last = 0;
}

static void fb_back_alloc(void)
{
    size_t bytes = (size_t)g_fb.width * g_fb.height * 4;
    g_back = (uint32_t *)kmalloc(bytes);
    g_nr = 0; g_last = -1;
    if (g_back)
        fb_invalidate();
}

void fb_mark_dirty(int x, int y, int w, int h)
{
    if (!g_fb.present || w <= 0 || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > (int)g_fb.width)  x1 = (int)g_fb.width;
    int y1 = y + h; if (y1 > (int)g_fb.height) y1 = (int)g_fb.height;
    if (x0 >= x1 || y0 >= y1)
        return;
    dirty_add(x0, y0, x1, y1);
}

void fb_invalidate(void)
{
    if (!g_fb.present)
        return;
    g_nr = 0; g_last = -1;
    dirty_add(0, 0, (int)g_fb.width, (int)g_fb.height);
}

static void present_rect(int x0, int y0, int x1, int y1)
{
    if (!g_back)
        return;
    int n = x1 - x0;
    if (n <= 0)
        return;
    /* Fast path: 32bpp scanout with no padding between rows, which is what
       both the VBE frame buffer and the VMSVGA scanout give us. Copying a
       whole row at once is several times cheaper than the per-pixel loop
       and it is the single hottest loop while a window is being dragged. */
    if (g_fb.bytes_pp == 4 && g_fb.pitch == g_fb.width * 4) {
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *dst = (uint32_t *)(g_fb.addr + (uint64_t)yy * g_fb.pitch) + x0;
            const uint32_t *src = g_back + (uint64_t)yy * g_fb.width + x0;
            __builtin_memcpy(dst, src, (size_t)n * 4);
        }
        return;
    }
    for (int yy = y0; yy < y1; yy++) {
        uint8_t *dst = g_fb.addr + (uint64_t)yy * g_fb.pitch;
        const uint32_t *src = g_back + (uint64_t)yy * g_fb.width + x0;
        if (g_fb.bytes_pp == 4) {
            for (int i = 0; i < n; i++)
                ((uint32_t *)dst)[x0 + i] = src[i];
        } else if (g_fb.bytes_pp == 2) {
            for (int i = 0; i < n; i++)
                ((uint16_t *)(dst + x0 * 2))[i] = (uint16_t)src[i];
        } else {
            for (int i = 0; i < n; i++) {
                uint32_t c = src[i];
                uint8_t *pp = dst + (x0 + i) * g_fb.bytes_pp;
                pp[0] = (uint8_t)((c >> g_fb.bpos) & 0xff);
                pp[1] = (uint8_t)((c >> g_fb.gpos) & 0xff);
                pp[2] = (uint8_t)((c >> g_fb.rpos) & 0xff);
            }
        }
    }
}

void fb_present_dirty(void)
{
    if (!g_fb.present || g_nr == 0)
        return;
    for (int i = 0; i < g_nr; i++) {
        int rx = g_rects[i].x0, ry = g_rects[i].y0;
        int rw = g_rects[i].x1 - rx, rh = g_rects[i].y1 - ry;
        if (rw <= 0 || rh <= 0)
            continue;
        present_rect(rx, ry, g_rects[i].x1, g_rects[i].y1);
        if (g_fb.gpu)
            svga_update_rect(rx, ry, rw, rh);
    }
    g_nr = 0; g_last = -1;
}

void fb_present_all(void)
{
    if (!g_fb.present)
        return;
    if (g_back)
        present_rect(0, 0, (int)g_fb.width, (int)g_fb.height);
    g_nr = 0; g_last = -1;
    if (g_fb.gpu)
        svga_update_rect(0, 0, (int)g_fb.width, (int)g_fb.height);
}

/* Rebind the front buffer to the GPU framebuffer handed over by the SVGA
   device after native mode takeover. The virtual window stays at FB_VBASE;
   only the backing physical pages change. Always 32bpp BGRX in SVGA mode. */
int fb_rebind_gpu(uint64_t phys, uint32_t width, uint32_t height,
                  uint32_t pitch)
{
    if (!g_fb.present || phys == 0 || width == 0 || height == 0)
        return 0;
    fb_clamp_mode(&width, &height);

    g_fb.width = width;
    g_fb.height = height;
    g_fb.pitch = pitch ? pitch : width * 4;
    g_fb.bpp = 32;
    g_fb.bytes_pp = 4;
    g_fb.rpos = 16; g_fb.rsize = 8;
    g_fb.gpos = 8;  g_fb.gsize = 8;
    g_fb.bpos = 0;  g_fb.bsize = 8;

    uint64_t size = (uint64_t)g_fb.pitch * (uint64_t)height;
    uint64_t pages = (size + 4095) / 4096;
    for (uint64_t p = 0; p < pages; p++)
        vmm_map_page(FB_VBASE + p * 4096, phys + p * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PWT);
    g_fb.addr = (uint8_t *)FB_VBASE;
    g_fb.gpu = 1;
    fb_back_alloc();
    fb_invalidate();

    serial_puts("[FB] rebound front buffer to GPU scanout phys=");
    serial_puthex(phys);
    serial_puts(" pitch=");
    serial_puthex(g_fb.pitch);
    serial_putc('\n');
    return 1;
}

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

static uint16_t vbe_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static uint32_t svga_read(uint16_t index)
{
    outw(SVGA_INDEX_PORT, index);
    return inl(SVGA_VALUE_PORT);
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
    fb_clamp_mode(&width, &height);
    int found = 0;
    int is_vmsvga = 0;
    uint8_t bus = 0, dev = 0, func = 0;
    for (uint8_t d = 0; d < 32; d++) {
        uint32_t id = pci_read(0, d, 0, 0);
        uint16_t vendor = (uint16_t)(id & 0xffff);
        uint16_t device = (uint16_t)(id >> 16);
        int is_bochs = (vendor == BOCHS_VENDOR && device == BOCHS_DEVICE);
        int is_vbox = (vendor == VBOX_VENDOR && device == VBOX_DEVICE);
        int is_vga = (vendor == VMSVGA_VENDOR && device == VMSVGA_DEVICE);
        if (is_bochs || is_vbox || is_vga) {
            found = 1;
            dev = d;
            if (is_vga)
                is_vmsvga = 1;
            break;
        }
    }
    if (!found) {
        serial_puts("VGA: no bochs/vbox/vmsvga device\n");
        return 0;
    }

    uint32_t cmd = pci_read(bus, dev, func, 4);
    cmd |= (1u << 1) | (1u << 2);
    pci_write(bus, dev, func, 4, cmd);

    uint32_t bar = pci_read(bus, dev, func, 0x10);
    uint64_t fbphys = bar & ~0xfull;
    if (is_vmsvga) {
        /* VMSVGA: framebuffer physical address comes from SVGA_REG_FB_START */
        uint32_t fbstart = svga_read(SVGA_REG_FB_START);
        if (fbstart != 0)
            fbphys = fbstart;
    }
    if (fbphys == 0)
        fbphys = 0xe0000000ull;

    vbe_write(VBE_DISPI_INDEX_ENABLE, 0);
    vbe_write(VBE_DISPI_INDEX_ID, 0xb0c0);
    vbe_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    vbe_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    vbe_write(VBE_DISPI_INDEX_BPP, 32);
    vbe_write(VBE_DISPI_INDEX_ENABLE,
              VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    uint32_t aw = vbe_read(VBE_DISPI_INDEX_XRES);
    uint32_t ah = vbe_read(VBE_DISPI_INDEX_YRES);
    if (aw == 0)
        aw = width;
    if (ah == 0)
        ah = height;

    g_fb.width = aw;
    g_fb.height = ah;
    g_fb.pitch = aw * 4;
    g_fb.bpp = 32;
    g_fb.bytes_pp = 4;
    g_fb.rpos = 16; g_fb.rsize = 8;
    g_fb.gpos = 8;  g_fb.gsize = 8;
    g_fb.bpos = 0;  g_fb.bsize = 8;

    uint64_t size = (uint64_t)g_fb.pitch * ah;
    uint64_t pages = (size + 4095) / 4096;
    for (uint64_t p = 0; p < pages; p++)
        vmm_map_page(FB_VBASE + p * 4096, fbphys + p * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PWT);
    g_fb.addr = (uint8_t *)FB_VBASE;
    g_fb.present = 1;
    fb_back_alloc();

    serial_puts(is_vmsvga ? "vmsvga framebuffer want="
                          : "bochs framebuffer want=");
    serial_puthex(width); serial_putc('x'); serial_puthex(height);
    serial_puts(" got=");
    serial_puthex(aw); serial_putc('x'); serial_puthex(ah);
    serial_puts(" fbphys="); serial_puthex(fbphys);
    serial_putc('\n');
    return 1;
}

int fb_linear_init(uint64_t phys, uint32_t width, uint32_t height,
                  uint32_t pitch, uint8_t bpp,
                  uint8_t rpos, uint8_t rsize,
                  uint8_t gpos, uint8_t gsize,
                  uint8_t bpos, uint8_t bsize)
{
    if (phys == 0 || width == 0 || height == 0 || bpp < 16)
        return 0;
    fb_clamp_mode(&width, &height);

    g_fb.width = width;
    g_fb.height = height;
    g_fb.bpp = bpp;
    g_fb.bytes_pp = (uint8_t)((bpp + 7) / 8);
    if (g_fb.bytes_pp < 2)
        g_fb.bytes_pp = 2;
    if (g_fb.bytes_pp > 4)
        g_fb.bytes_pp = 4;
    g_fb.pitch = pitch ? pitch : width * g_fb.bytes_pp;

    /* PC VBE/BOCHS/SVGA 的 32bpp 帧缓冲事实标准是 BGRX（蓝在最低字节，
       r16/g8/b0）。部分 bootloader/虚拟显卡（如 VBox VMSVGA）经 multiboot
       上报的 RGB 位域异常，会导致 fb_rgb 通道错位、蓝色显示成绿色。
       仅接受两种已知标准布局，其余一律校正为 BGRX。 */
    if (bpp == 32 || bpp == 24) {
        int std_bgr = (rpos == 16 && gpos == 8 && bpos == 0);
        int std_rgb = (rpos == 0 && gpos == 8 && bpos == 16);
        if (!std_bgr && !std_rgb) {
            serial_puts("[FB] reported RGB order ");
            serial_putc('0' + rpos); serial_putc('/');
            serial_putc('0' + gpos); serial_putc('/');
            serial_putc('0' + bpos);
            serial_puts(" nonstandard, forcing BGRX 16/8/0\n");
            rpos = 16; gpos = 8; bpos = 0;
        }
        rsize = gsize = bsize = 8;
    }

    g_fb.rpos = rpos; g_fb.rsize = rsize ? rsize : 8;
    g_fb.gpos = gpos; g_fb.gsize = gsize ? gsize : 8;
    g_fb.bpos = bpos; g_fb.bsize = bsize ? bsize : 8;

    uint64_t size = (uint64_t)g_fb.pitch * (uint64_t)height;
    uint64_t pages = (size + 4095) / 4096;
    for (uint64_t p = 0; p < pages; p++)
        vmm_map_page(FB_VBASE + p * 4096, phys + p * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PWT);
    g_fb.addr = (uint8_t *)FB_VBASE;
    g_fb.present = 1;
    fb_back_alloc();

    serial_puts("linear fb ");
    serial_puthex(width); serial_putc('x'); serial_puthex(height);
    serial_puts(" bpp="); serial_puthex(bpp);
    serial_puts(" pitch="); serial_puthex(g_fb.pitch);
    serial_puts(" rgb="); serial_putc('0' + rpos); serial_putc('/');
    serial_putc('0' + gpos); serial_putc('/'); serial_putc('0' + bpos);
    serial_puts(" phys="); serial_puthex(phys);
    serial_putc('\n');
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
    if (!g_fb.present || !g_back || x < 0 || y < 0
        || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height)
        return;
    g_back[(uint64_t)y * g_fb.width + x] = packed;
    dirty_add(x, y, x + 1, y + 1);
}

uint32_t fb_getpixel(int x, int y)
{
    if (!g_fb.present || !g_back || x < 0 || y < 0
        || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height)
        return 0;
    return g_back[(uint64_t)y * g_fb.width + x];
}

/* Raw row pointer into the back buffer. The 3D rasterizer writes through it
   instead of calling fb_putpixel per pixel (which re-checks the bounds and
   merges the dirty rect every single pixel). The caller marks the dirty rect
   once after the whole viewport is drawn. */
uint32_t *fb_back_row(int y)
{
    if (!g_fb.present || !g_back || y < 0 || (uint32_t)y >= g_fb.height)
        return 0;
    return g_back + (uint64_t)y * g_fb.width;
}

static void unpack(uint32_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)((c >> g_fb.rpos) & 0xff);
    *g = (uint8_t)((c >> g_fb.gpos) & 0xff);
    *b = (uint8_t)((c >> g_fb.bpos) & 0xff);
}

uint32_t fb_mix(uint32_t fg, uint32_t bg, uint8_t cover)
{
    if (cover >= 255)
        return fg;
    if (cover == 0)
        return bg;
    uint8_t fr, fg_, fb_, br, bg_, bb_;
    unpack(fg, &fr, &fg_, &fb_);
    unpack(bg, &br, &bg_, &bb_);
    unsigned a = cover, ia = 255 - a;
    uint8_t r = (uint8_t)((fr * a + br * ia) / 255);
    uint8_t g = (uint8_t)((fg_ * a + bg_ * ia) / 255);
    uint8_t b = (uint8_t)((fb_ * a + bb_ * ia) / 255);
    return fb_rgb(r, g, b);
}

void fb_putpixel_blend(int x, int y, uint32_t fg, uint8_t cover)
{
    if (!g_fb.present || x < 0 || y < 0
        || (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height)
        return;
    fb_putpixel(x, y, fb_mix(fg, fb_getpixel(x, y), cover));
}

void fb_clear(uint32_t packed)
{
    if (!g_fb.present || !g_back)
        return;
    size_t n = (size_t)g_fb.width * g_fb.height;
    for (size_t i = 0; i < n; i++)
        g_back[i] = packed;
    fb_invalidate();
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t packed)
{
    if (!g_fb.present || !g_back || w <= 0 || h <= 0)
        return;
    int W = (int)g_fb.width, H = (int)g_fb.height;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > W) x1 = W;
    int y1 = y + h; if (y1 > H) y1 = H;
    if (x0 >= x1 || y0 >= y1)
        return;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *p = g_back + (uint64_t)yy * W + x0;
        int cnt = x1 - x0;
        for (int xx = 0; xx < cnt; xx++)
            p[xx] = packed;
    }
    fb_mark_dirty(x0, y0, x1 - x0, y1 - y0);
}

void fb_blit(int dx, int dy, int w, int h, const uint32_t *pixels)
{
    if (!g_fb.present || !g_back)
        return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_putpixel(dx + i, dy + j, pixels[j * w + i]);
}

void fb_blit_full(const uint32_t *pixels)
{
    if (!g_fb.present || !g_back)
        return;
    __builtin_memcpy(g_back, pixels, (size_t)g_fb.width * g_fb.height * 4);
    fb_invalidate();
}

/* Rectangle copy from a source surface that may be wider than the rectangle.
   Writes straight into the back buffer and marks one dirty rect, so a window
   drag repaints only its own old+new footprint. */
void fb_blit_rect(int dx, int dy, int w, int h, const uint32_t *src,
                  int src_stride)
{
    if (!g_fb.present || !g_back || !src || w <= 0 || h <= 0)
        return;
    int W = (int)g_fb.width, H = (int)g_fb.height;
    int sx = 0, sy = 0;
    if (dx < 0) { sx = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy = -dy; h += dy; dy = 0; }
    if (dx + w > W) w = W - dx;
    if (dy + h > H) h = H - dy;
    if (w <= 0 || h <= 0)
        return;
    for (int j = 0; j < h; j++) {
        const uint32_t *s = src + (uint64_t)(sy + j) * src_stride + sx;
        uint32_t *d = g_back + (uint64_t)(dy + j) * W + dx;
        __builtin_memcpy(d, s, (size_t)w * 4);
    }
    fb_mark_dirty(dx, dy, w, h);
}
