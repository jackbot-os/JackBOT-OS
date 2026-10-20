#include <stdint.h>
#include <stddef.h>
#include <kernel/svga.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>

/* VMware SVGA II device protocol (PCI 15ad:0405). The register and
   FIFO layout follows the public VMware SVGA device interface; it is
   the same device VirtualBox emulates with --graphicscontroller vmsvga. */

#define SVGA_INDEX_PORT   0x5650
#define SVGA_VALUE_PORT   0x5654

#define SVGA_ID_INVALID   0xFFFFFFFF
#define SVGA_MAGIC        0x900000U
#define SVGA_ID_0         (SVGA_MAGIC << 8 | 0)
#define SVGA_ID_2         (SVGA_MAGIC << 8 | 2)

#define SVGA_REG_ID             0
#define SVGA_REG_ENABLE         1
#define SVGA_REG_WIDTH          2
#define SVGA_REG_HEIGHT         3
#define SVGA_REG_MAX_WIDTH      4
#define SVGA_REG_MAX_HEIGHT     5
#define SVGA_REG_DEPTH          6
#define SVGA_REG_BPP            7
#define SVGA_REG_BYTES_PER_LINE 12
#define SVGA_REG_FB_START       13
#define SVGA_REG_FB_OFFSET      14
#define SVGA_REG_VRAM_SIZE      15
#define SVGA_REG_FB_SIZE        16
#define SVGA_REG_CAPABILITIES   17
#define SVGA_REG_FIFO_START     18
#define SVGA_REG_FIFO_SIZE      19
#define SVGA_REG_CONFIG_DONE    20
#define SVGA_REG_SYNC           21
#define SVGA_REG_BUSY           22
#define SVGA_REG_GUEST_ID       23
#define SVGA_REG_CURSOR_X       25
#define SVGA_REG_CURSOR_Y       26
#define SVGA_REG_CURSOR_ON      27
#define SVGA_REG_CAP2           59
#define SVGA_REG_DEV_CAP        29
#define SVGA_REG_DEV_CAP_COUNT  30

#define SVGA_CAP2_GMR          (1u << 25)
#define SVGA_CAP2_GMR2         (1u << 29)
#define SVGA_CAP2_3D           (1u << 30)
#define SVGA_CAP2_EXT_FIFO     (1u << 31)

#define SVGA_FIFO_MIN        0
#define SVGA_FIFO_MAX        1
#define SVGA_FIFO_NEXT_CMD   2
#define SVGA_FIFO_STOP       3
#define SVGA_FIFO_CAP        4
#define SVGA_FIFO_FLAGS      5
#define SVGA_FIFO_FENCE      6
#define SVGA_FIFO_NUM_REGS   30
#define SVGA_FIFO_MIN_BYTES  (SVGA_FIFO_NUM_REGS * 4)

#define SVGA_CMD_UPDATE              1
#define SVGA_CMD_DEFINE_ALPHA_CURSOR 22
#define SVGA_CMD_MOVE_CURSOR         23
#define SVGA_CMD_BEGIN_3D     28
#define SVGA_CMD_END_3D       29
#define SVGA_CMD_CREATE_CTX   40
#define SVGA_CMD_DESTROY_CTX  41

#define SVGA3D_DEVCAP_3D      0

#define SVGA_FIFO_VBASE 0xffff800082000000ull

static struct {
    int present;
    int id2;
    int fifo_live;
    int taken;
    int has_3d;
    int has_gmr;
    uint32_t cap2;
    uint32_t fb_start, fb_size;
    uint32_t fifo_start, fifo_size;
    uint32_t max_w, max_h;
    uint32_t mode_w, mode_h, pitch;
    volatile uint32_t *fifo;
} g;

static uint16_t io_index = SVGA_INDEX_PORT;
static uint16_t io_value = SVGA_VALUE_PORT;

static uint32_t reg_read(uint32_t idx);
static void reg_write(uint32_t idx, uint32_t v);

/* ---- Hardware cursor (VMSVGA DEFINE_ALPHA_CURSOR / MOVE_CURSOR) ----
   The cursor is composited by the GPU itself, so it never touches the
   scanout/back buffer and cannot leave drag trails. One black-filled,
   white-edged arrow, generated once as 32bpp BGRA (alpha in top byte). */
#define CUR_W 24
#define CUR_H 24
static uint32_t g_cur_pixels[CUR_W * CUR_H];
static int g_cur_ready;       /* bitmap built */
static int g_cur_defined;     /* DEFINE_ALPHA_CURSOR sent to device */
static int g_cur_visible;
static int g_cur_x, g_cur_y;
static int g_cur_need_move;

/* Arrow silhouette (21x20); the hotspot is the tip at (0,0). */
static const char *const cur_art[20] = {
    "X....................",
    "XX...................",
    "XXX..................",
    "XXXX.................",
    "XXXXX................",
    "XXXXXX...............",
    "XXXXXXX..............",
    "XXXXXXXX.............",
    "XXXXXXXXX............",
    "XXXXXXXXXX...........",
    "XXXXXXXXXXX..........",
    "XXXXXXXXXXXX.........",
    "XXXXXXXXXXXXX........",
    "XXXXXXXXXXXX.........",
    "XXXXXXXXXXX..........",
    "XXXXXX.XXXXX.........",
    "XXXXX...XXXX.........",
    "XXXX.....XXX.........",
    "XXX.......XX.........",
    "XX.........X.........",
};

static void cursor_build(void)
{
    if (g_cur_ready)
        return;
    for (int yy = 0; yy < CUR_H; yy++)
        for (int xx = 0; xx < CUR_W; xx++)
            g_cur_pixels[yy * CUR_W + xx] = 0; /* fully transparent */
    for (int yy = 0; yy < 20; yy++) {
        for (int xx = 0; xx < 21; xx++) {
            if (cur_art[yy][xx] != 'X')
                continue;
            /* opaque black body */
            g_cur_pixels[yy * CUR_W + xx] = 0xff000000u;
        }
    }
    /* white edge: any transparent pixel 8-adjacent to the body */
    for (int yy = 0; yy < 20; yy++) {
        for (int xx = 0; xx < 21; xx++) {
            if (g_cur_pixels[yy * CUR_W + xx] != 0)
                continue;
            int body = 0;
            for (int dy = -1; dy <= 1 && !body; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = xx + dx, ny = yy + dy;
                    if (nx < 0 || ny < 0 || nx >= 21 || ny >= 20)
                        continue;
                    if (cur_art[ny][nx] == 'X') { body = 1; break; }
                }
            if (body)
                g_cur_pixels[yy * CUR_W + xx] = 0xffffffffu;
        }
    }
    g_cur_ready = 1;
}

void svga_cursor_init(void)
{
    if (!g.taken)
        return;
    cursor_build();
    g_cur_defined = 0;
    g_cur_visible = 1;
    g_cur_need_move = 1;
    reg_write(SVGA_REG_CURSOR_ON, 1);
    serial_puts("[SVGA] HW cursor armed\n");
}

void svga_cursor_show(int visible)
{
    if (!g.taken)
        return;
    reg_write(SVGA_REG_CURSOR_ON, visible ? 1 : 0);
    if (g_cur_visible != visible) {
        g_cur_visible = visible;
        g_cur_defined = 0; /* redefine to show/hide */
    }
}

void svga_cursor_move(int x, int y)
{
    if (!g.taken)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)g.mode_w) x = (int)g.mode_w - 1;
    if (y >= (int)g.mode_h) y = (int)g.mode_h - 1;
    if (x != g_cur_x || y != g_cur_y || g_cur_need_move) {
        g_cur_x = x;
        g_cur_y = y;
        g_cur_need_move = 1;
    }
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

/* SVGA register index/value ports are 32-bit wide. Using 16-bit outs on the
   index port (as the older probe did) is not latched correctly by VBox's
   VMSVGA, which made the ID=2 negotiation read back as invalid. */
static uint32_t reg_read(uint32_t idx)
{
    outl(io_index, idx);
    return inl(io_value);
}

static void reg_write(uint32_t idx, uint32_t v)
{
    outl(io_index, idx);
    outl(io_value, v);
}

/* VBox's VMSVGA only responds to its register I/O ports after the PCI
   command register enables I/O space (bit0) and memory space (bit1).
   Prefer the real I/O base from BAR0, falling back to the legacy 0x5650.
   On the SVGA II (device 0x0405) the index/value ports are packed at byte
   offsets +0/+1 (per the QEMU vmsvga SVGA2 SVGA_IO_MUL=1 layout). */
static void prepare_io(struct pci_dev *d)
{
    uint16_t before = pci_cfg_read16(d->bus, d->dev, d->func, 4);
    pci_cfg_write16(d->bus, d->dev, d->func, 4, before | 0x07);
    uint16_t after = pci_cfg_read16(d->bus, d->dev, d->func, 4);
    uint32_t bar0 = d->bar[0];
    for (int bn = 0; bn < 6; bn++) {
        uint32_t bv = pci_cfg_read32(d->bus, d->dev, d->func, 0x10 + bn * 4);
        serial_puts("[SVGA] BAR"); serial_putc('0' + bn);
        serial_puts("="); serial_puthex(bv); serial_putc(' ');
    }
    serial_putc('\n');
    /* Pick whichever BAR is a memory-decoded I/O region (bit0 set). */
    for (int bn = 0; bn < 6; bn++) {
        uint32_t bv = pci_cfg_read32(d->bus, d->dev, d->func, 0x10 + bn * 4);
        if (bv & 0x1) {
            uint32_t base = bv & ~0x3u;
            if (base) {
                io_index = (uint16_t)(base + 0);
                io_value = (uint16_t)(base + 1);
                serial_puts("[SVGA] I/O ports from BAR");
                serial_putc('0' + bn); serial_putc(' ');
                serial_puthex(base); serial_putc('\n');
            }
            break;
        }
    }
    serial_puts("[SVGA] pci cmd="); serial_puthex(after);
    serial_puts(" bar0="); serial_puthex(bar0);
    serial_puts(" io_idx="); serial_puthex(io_index);
    serial_putc('\n');
}

/* One-shot port discovery: try the BAR0 I/O region and the legacy 0x5650
   base with each plausible value-port stride, write ID=2, and read ID back.
   The first pair that reads 2 is latched into io_index/io_value. */
static void probe_ports(uint16_t bar_base)
{
    uint16_t bases[2] = { bar_base, SVGA_INDEX_PORT };
    int strides[3] = { 1, 4, 8 };
    int found = 0;
    for (int b = 0; b < 2 && !found; b++) {
        if (!bases[b])
            continue;
        for (int s = 0; s < 3 && !found; s++) {
            uint16_t idx = bases[b];
            uint16_t val = idx + strides[s];
            outl(idx, SVGA_REG_ID);
            outl(val, SVGA_ID_0);
            outl(idx, SVGA_REG_ID);
            outl(val, SVGA_ID_2);
            outl(idx, SVGA_REG_ID);
            uint32_t r = inl(val);
            serial_puts("[SVGA] probe idx="); serial_puthex(idx);
            serial_puts(" val="); serial_puthex(val);
            serial_puts(" id="); serial_puthex(r); serial_putc('\n');
            outl(idx, SVGA_REG_ID);
            outl(val, SVGA_ID_0);
            if (r == SVGA_ID_2) {
                io_index = idx;
                io_value = val;
                found = 1;
            }
        }
    }
    if (!found) {
        io_index = bar_base ? bar_base : SVGA_INDEX_PORT;
        io_value = io_index + 1;
    }
}

static void map_fifo(void)
{
    uint64_t pages = (g.fifo_size + 4095) / 4096;
    for (uint64_t p = 0; p < pages; p++)
        vmm_map_page(SVGA_FIFO_VBASE + p * 4096,
                     (uint64_t)g.fifo_start + p * 4096,
                     PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT);
    g.fifo = (volatile uint32_t *)SVGA_FIFO_VBASE;
}

/* Reserved for G2 (full mode takeover): the FIFO startup sequence is
   written out now so the protocol hand-off is explicit, but it is not
   called while the display is still driven through the VBE path. */
static void fifo_init(void)
{
    if (!g.fifo)
        return;
    g.fifo[SVGA_FIFO_MIN] = SVGA_FIFO_MIN_BYTES;
    g.fifo[SVGA_FIFO_MAX] = g.fifo_size;
    g.fifo[SVGA_FIFO_NEXT_CMD] = SVGA_FIFO_MIN_BYTES;
    g.fifo[SVGA_FIFO_STOP] = SVGA_FIFO_MIN_BYTES;
    g.fifo[SVGA_FIFO_CAP] = 0;
    g.fifo[SVGA_FIFO_FLAGS] = 0;
    g.fifo[SVGA_FIFO_FENCE] = 0;
    g.fifo[SVGA_FIFO_NEXT_CMD] = SVGA_FIFO_MIN_BYTES;
    g.fifo[SVGA_FIFO_STOP] = SVGA_FIFO_MIN_BYTES;
    /* CONFIG_DONE arms the command FIFO; cursor/update commands are
       ignored by the host until this is written. */
    reg_write(SVGA_REG_CONFIG_DONE, 1);
    g.fifo_live = 1;
}

static uint32_t devcap_read(uint32_t index)
{
    reg_write(SVGA_REG_DEV_CAP, index);
    return reg_read(SVGA_REG_DEV_CAP);
}

static void read_caps(void)
{
    g.cap2 = reg_read(SVGA_REG_CAP2);
    g.max_w = reg_read(SVGA_REG_MAX_WIDTH);
    g.max_h = reg_read(SVGA_REG_MAX_HEIGHT);
    g.has_gmr = (g.cap2 & SVGA_CAP2_GMR) || (g.cap2 & SVGA_CAP2_GMR2);
    uint32_t d3d = devcap_read(SVGA3D_DEVCAP_3D);
    g.has_3d = ((g.cap2 & SVGA_CAP2_3D) != 0) && (d3d != 0);
}

int svga_gpu_init(void)
{
    /* Once the device owns the scanout, never renegotiate ID here: doing so
       would tear down the live mode and blank the display. */
    if (g.taken)
        return 1;

    struct pci_dev *d = pci_find(0x15ad, 0x0405);
    if (!d) {
        serial_puts("[SVGA] no VMSVGA device\n");
        return 0;
    }
    pci_enable_bus_master(d);
    prepare_io(d);
    if (d->bar[0] & 0x1)
        probe_ports((uint16_t)pci_bar_io(d, 0));

    /* Read-only probe: negotiate SVGA II, read geometry/caps, then
       restore ID so the working VBE display path is not disturbed. */
    uint32_t orig_id = reg_read(SVGA_REG_ID);
    reg_write(SVGA_REG_ID, SVGA_ID_2);
    uint32_t negotiated = reg_read(SVGA_REG_ID);
    g.id2 = (negotiated == SVGA_ID_2);

    if (g.id2) {
        g.fb_start = reg_read(SVGA_REG_FB_START);
        g.fb_size = reg_read(SVGA_REG_FB_SIZE);
        g.fifo_start = reg_read(SVGA_REG_FIFO_START);
        g.fifo_size = reg_read(SVGA_REG_FIFO_SIZE);
        read_caps();
    }
    reg_write(SVGA_REG_ID, orig_id);

    g.present = 1;
    svga_gpu_report();
    return 1;
}

/* G2: take the display out of VBE and into native SVGA II mode. On success
   the device owns the scanout and presents via FIFO SVGA_CMD_UPDATE; the
   returned framebuffer physical address is then bound by the fb layer.
   Every failure path happens before ENABLE, so the VBE/multiboot display
   keeps working and the caller simply stays on the software path. */
int svga_gpu_takeover(uint32_t w, uint32_t h,
                      uint64_t *out_fbphys, uint32_t *out_pitch,
                      uint32_t *out_fbsize)
{
    if (g.taken)
        return 1;

    struct pci_dev *d = pci_find(0x15ad, 0x0405);
    if (!d) {
        serial_puts("[SVGA] takeover: device not found, stay on VBE\n");
        return 0;
    }
    pci_enable_bus_master(d);
    prepare_io(d);
    uint16_t bar_base = 0;
    if (d->bar[0] & 0x1)
        bar_base = (uint16_t)pci_bar_io(d, 0);
    probe_ports(bar_base);

    /* Negotiate SVGA II. A read-back that is not 2 means this device does
       not accept native mode in its current state; leave VBE untouched. */
    reg_write(SVGA_REG_ENABLE, 0);
    reg_write(SVGA_REG_ID, SVGA_ID_2);
    uint32_t idv = reg_read(SVGA_REG_ID);
    if (idv != SVGA_ID_2) {
        serial_puts("[SVGA] takeover: ID readback=");
        serial_puthex(idv);
        serial_puts(" rejected, stay on VBE\n");
        reg_write(SVGA_REG_ID, SVGA_ID_0);
        return 0;
    }
    g.id2 = 1;

    uint32_t pitch = w * 4;
    reg_write(SVGA_REG_WIDTH, w);
    reg_write(SVGA_REG_HEIGHT, h);
    reg_write(SVGA_REG_BPP, 32);
    reg_write(SVGA_REG_DEPTH, 24);
    reg_write(SVGA_REG_BYTES_PER_LINE, pitch);
    reg_write(SVGA_REG_ENABLE, 1);

    if (reg_read(SVGA_REG_WIDTH) != w ||
        reg_read(SVGA_REG_HEIGHT) != h) {
        serial_puts("[SVGA] takeover: mode set failed, restore VBE\n");
        reg_write(SVGA_REG_ENABLE, 0);
        reg_write(SVGA_REG_ID, SVGA_ID_0);
        return 0;
    }

    g.fb_start = reg_read(SVGA_REG_FB_START);
    g.fb_size = reg_read(SVGA_REG_FB_SIZE);
    g.fifo_start = reg_read(SVGA_REG_FIFO_START);
    g.fifo_size = reg_read(SVGA_REG_FIFO_SIZE);
    read_caps();

    if (g.fb_start == 0 || g.fb_size < pitch * h) {
        serial_puts("[SVGA] takeover: framebuffer too small, restore VBE\n");
        reg_write(SVGA_REG_ENABLE, 0);
        reg_write(SVGA_REG_ID, SVGA_ID_0);
        return 0;
    }
    if (g.fifo_start == 0 || g.fifo_size < SVGA_FIFO_MIN_BYTES) {
        serial_puts("[SVGA] takeover: no command FIFO, restore VBE\n");
        reg_write(SVGA_REG_ENABLE, 0);
        reg_write(SVGA_REG_ID, SVGA_ID_0);
        return 0;
    }

    map_fifo();
    if (!g.fifo) {
        serial_puts("[SVGA] takeover: FIFO map failed, restore VBE\n");
        reg_write(SVGA_REG_ENABLE, 0);
        reg_write(SVGA_REG_ID, SVGA_ID_0);
        return 0;
    }
    fifo_init();

    g.mode_w = w;
    g.mode_h = h;
    g.pitch = pitch;
    g.taken = 1;
    g.present = 1;

    if (out_fbphys) *out_fbphys = (uint64_t)g.fb_start + reg_read(SVGA_REG_FB_OFFSET);
    if (out_pitch) *out_pitch = pitch;
    if (out_fbsize) *out_fbsize = g.fb_size;

    serial_puts("[SVGA] GPU scanout ACTIVE ");
    serial_puthex(w); serial_putc('x'); serial_puthex(h);
    serial_puts(" fbphys="); serial_puthex(g.fb_start);
    serial_puts(" fbsize="); serial_puthex(g.fb_size);
    serial_puts(" fifo="); serial_puthex(g.fifo_size);
    serial_putc('\n');

    g_cur_x = (int)w / 2;
    g_cur_y = (int)h / 2;
    svga_cursor_init();
    return 1;
}

int svga_gpu_present(void)      { return g.taken; }
int svga_gpu_3d_present(void)   { return g.has_3d; }

/* 2D accelerated present: submit one UPDATE rectangle and wait for the
   device to finish scanning it. Each command is laid down at the FIFO base
   and the ring is re-used after the SYNCCPU wait, so no wrap handling is
   needed for the single in-flight present. */
/* Lay a DEFINE_ALPHA_CURSOR at the given byte offset; returns the new
   byte offset. Hidden cursor is a fully transparent bitmap. */
static uint32_t emit_cursor_define(uint32_t off, int visible)
{
    volatile uint32_t *p = g.fifo + (off / 4);
    uint32_t *px = g_cur_pixels;
    p[0] = SVGA_CMD_DEFINE_ALPHA_CURSOR;
    p[1] = 0;               /* cursor id */
    p[2] = 0;               /* hotspot x */
    p[3] = 0;               /* hotspot y */
    p[4] = CUR_W;
    p[5] = CUR_H;
    for (int i = 0; i < CUR_W * CUR_H; i++)
        p[6 + i] = visible ? px[i] : 0;
    serial_puts("[SVGA] HW cursor DEFINE sent\n");
    return off + (uint32_t)(6 + CUR_W * CUR_H) * 4;
}

static uint32_t emit_cursor_move(uint32_t off, int x, int y)
{
    volatile uint32_t *p = g.fifo + (off / 4);
    p[0] = SVGA_CMD_MOVE_CURSOR;
    p[1] = (uint32_t)x;
    p[2] = (uint32_t)y;
    static int n = 0;
    if (n < 6) { serial_puts("[SVGA] HW cursor MOVE\n"); n++; }
    return off + 3 * 4;
}

void svga_update_rect(int x, int y, int w, int h)
{
    if (!g.taken || !g.fifo)
        return;
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)g.mode_w) w = (int)g.mode_w - x;
    if (y + h > (int)g.mode_h) h = (int)g.mode_h - y;
    if (w <= 0 || h <= 0)
        return;

    cursor_build();

    /* Batch cursor define/move together with the UPDATE rect so the
       hardware cursor tracks the frame in one FIFO submission. */
    uint32_t off = SVGA_FIFO_MIN_BYTES;
    if (g_cur_ready && !g_cur_defined) {
        off = emit_cursor_define(off, g_cur_visible);
        g_cur_defined = 1;
    }
    if (g_cur_visible && g_cur_need_move) {
        off = emit_cursor_move(off, g_cur_x, g_cur_y);
        g_cur_need_move = 0;
    }

    volatile uint32_t *cmd = g.fifo + (off / 4);
    cmd[0] = SVGA_CMD_UPDATE;
    cmd[1] = (uint32_t)x;
    cmd[2] = (uint32_t)y;
    cmd[3] = (uint32_t)w;
    cmd[4] = (uint32_t)h;
    off += 5 * 4;

    g.fifo[SVGA_FIFO_NEXT_CMD] = off;
    g.fifo[SVGA_FIFO_STOP] = off;

    reg_write(SVGA_REG_SYNC, off);
    while (reg_read(SVGA_REG_BUSY))
        ;

    g.fifo[SVGA_FIFO_NEXT_CMD] = SVGA_FIFO_MIN_BYTES;
    g.fifo[SVGA_FIFO_STOP] = SVGA_FIFO_MIN_BYTES;
}

/* Standalone cursor submit (no screen dirty): used when the pointer moves
   over a static frame. Defines (if needed) and moves the cursor, then
   waits for the device. */
void svga_cursor_flush(void)
{
    if (!g.taken || !g.fifo)
        return;
    cursor_build();
    if (!g_cur_need_move && g_cur_defined)
        return;
    uint32_t off = SVGA_FIFO_MIN_BYTES;
    if (g_cur_ready && !g_cur_defined) {
        off = emit_cursor_define(off, g_cur_visible);
        g_cur_defined = 1;
    }
    if (g_cur_visible && g_cur_need_move) {
        off = emit_cursor_move(off, g_cur_x, g_cur_y);
        g_cur_need_move = 0;
    } else {
        return;
    }
    g.fifo[SVGA_FIFO_NEXT_CMD] = off;
    g.fifo[SVGA_FIFO_STOP] = off;
    reg_write(SVGA_REG_SYNC, off);
    while (reg_read(SVGA_REG_BUSY))
        ;
    g.fifo[SVGA_FIFO_NEXT_CMD] = SVGA_FIFO_MIN_BYTES;
    g.fifo[SVGA_FIFO_STOP] = SVGA_FIFO_MIN_BYTES;
}

void svga_gpu_sync(void)
{
    if (!g.taken)
        return;
}

/* G3 skeleton: a hardware 3D context is created with a BEGIN_3D /
   CREATE_CONTEXT pair; the surface/shader/state command sequence and
   the compositing pipeline are the long pole (see kernel/svga.c header
   notes and the GPU roadmap). Stubs stay honest until implemented. */
void svga_gpu_report(void)
{
    serial_puts("[SVGA] VMSVGA 15ad:0405, id2=");
    serial_puts(g.id2 ? "yes" : "no");
    serial_puts(" 3D=");
    serial_puts(g.has_3d ? "yes" : "no");
    serial_puts(" GMR=");
    serial_puts(g.has_gmr ? "yes" : "no");
    serial_puts(" fifo=");
    serial_puthex(g.fifo_size);
    serial_puts(" fb=");
    serial_puthex(g.fb_size);
    serial_puts(" max=");
    serial_puthex(g.max_w);
    serial_putc('x');
    serial_puthex(g.max_h);
    serial_puts(g.taken ? " GPU scanout active"
                        : (g.fifo_live ? " live"
                                       : " probe-only (VBE owns scanout)"));
    serial_putc('\n');
}
