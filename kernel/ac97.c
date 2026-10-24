#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/ac97.h>
#include <kernel/hal.h>
#include <kernel/pci.h>
#include <kernel/pmm.h>
#include <kernel/payload.h>
#include <kernel/serial.h>
#include <kernel/heap.h>
#include <kernel/fat32.h>

uint64_t timer_get_ticks(void);

#define NB   32
#define PAGE 4096

struct bdl_entry {
    uint32_t addr;
    uint16_t len;
    uint16_t flags;
} __attribute__((packed));

static uint16_t mixer_base, bm_base;
static int      present;
static uint32_t bdl_phys;

static const uint16_t ac97_ids[] = {
    0x2415, 0x2425, 0x2445, 0x2485, 0x24c5, 0x24d5, 0x27de, 0
};

static void ac_wait(void)
{
    uint32_t t = timer_get_ticks();
    while (hal_inb(bm_base + 0x34) & 1) {
        if (timer_get_ticks() - t > 2)
            break;
    }
}

static void codecw(uint8_t reg, uint16_t v)
{
    ac_wait();
    hal_outw(mixer_base + reg, v);
}

static uint16_t codecr(uint8_t reg)
{
    ac_wait();
    return hal_inw(mixer_base + reg);
}

static void ac_tick_wait(uint32_t n)
{
    if (n == 0) n = 1;
    uint64_t t = timer_get_ticks();
    while (timer_get_ticks() - t < n)
        __asm__ volatile("pause");
}

int ac97_present(void) { return present; }

static int alloc_dma_pages(void)
{
    void *bdl = pmm_alloc_page_below(0x40000000ull);
    if (!bdl)
        return 0;
    bdl_phys = (uint32_t)(uintptr_t)bdl;
    memset(bdl, 0, PAGE);
    return 1;
}

int ac97_init(void)
{
    struct pci_dev *d = 0;
    for (int i = 0; ac97_ids[i]; i++) {
        d = pci_find(0x8086, ac97_ids[i]);
        if (d)
            break;
    }
    if (!d) {
        serial_puts("[AC97] no controller found\n");
        return 0;
    }

    mixer_base = pci_bar_io(d, 0);
    bm_base    = pci_bar_io(d, 1);
    if (!mixer_base || !bm_base) {
        serial_puts("[AC97] bad BAR\n");
        return 0;
    }

    uint16_t cmd = pci_cfg_read16(d->bus, d->dev, d->func, 4);
    cmd |= 0x0005;
    pci_cfg_write16(d->bus, d->dev, d->func, 4, cmd);

    uint32_t g = hal_inl(bm_base + 0x2c);
    if ((g & 2) == 0)
        g |= 2;
    else
        g |= 4;
    g &= ~8u;
    hal_outl(bm_base + 0x2c, g);
    ac_tick_wait(2);

    hal_outb(bm_base + 0x0b, 0);
    hal_outb(bm_base + 0x1b, 0);
    hal_outb(bm_base + 0x2b, 0);
    hal_outb(bm_base + 0x7b, 0);

    if (!alloc_dma_pages()) {
        serial_puts("[AC97] dma alloc failed\n");
        return 0;
    }

    codecw(0x00, 0x8000);
    ac_tick_wait(1);
    codecw(0x02, 0x0000);
    codecw(0x04, 0x0000);
    codecw(0x06, 0x0000);
    codecw(0x18, 0x0000);
    codecw(0x24, 0x0000);

    uint16_t eid = codecr(0x28);
    uint16_t ext = codecr(0x2a);
    if (eid & 1) {
        codecw(0x2a, (uint16_t)(ext | 1));
        if (codecr(0x2a) & 1)
            codecw(0x2c, 48000);
    }

    uint16_t vid = codecr(0x7c);
    uint16_t did = codecr(0x7e);
    serial_puts("[AC97] codec id=");
    serial_puthex(vid);
    serial_puthex(did);
    serial_putc('\n');

    present = 1;
    serial_puts("[AC97] ready\n");
    return 1;
}

void ac97_play_pcm(const void *data, uint32_t bytes)
{
    if (!present || !data || bytes < 4)
        return;

    const int16_t *s = data;
    uint32_t samples = bytes / 2;
    uint32_t nbytes  = samples * 4;

    uint32_t need  = (nbytes + PAGE - 1) / PAGE;
    uint32_t alloc = need + 8;
    uint64_t *pg = kmalloc(alloc * sizeof(uint64_t));
    if (!pg)
        return;
    for (uint32_t i = 0; i < alloc; i++)
        pg[i] = 0;

    uint64_t first = 0, last = 0;
    int ok = 1;
    for (uint32_t i = 0; i < alloc; i++) {
        void *p = pmm_alloc_page_below(0x40000000ull);
        if (!p) { ok = 0; break; }
        pg[i] = (uint64_t)(uintptr_t)p;
        if (i == 0)
            first = pg[i];
        else if (pg[i] != last + PAGE)
            ok = 0;
        last = pg[i];
    }

    if (ok) {
        uint64_t base = (first + 0x7fffull) & ~0x7fffull;
        if (base + nbytes > last + PAGE)
            ok = 0;

        if (ok) {
            int16_t *dst = (void *)(uintptr_t)base;
            for (uint32_t i = 0; i < samples; i++) {
                int16_t v = s[i];
                dst[i * 2]     = v;
                dst[i * 2 + 1] = v;
            }

            hal_outb(bm_base + 0x1b, 0x02);
            ac_tick_wait(2);
            hal_outb(bm_base + 0x1b, 0x00);

            struct bdl_entry *bdl = (void *)(uintptr_t)bdl_phys;
            memset(bdl, 0, PAGE);
            int e = 0;
            uint32_t off = 0;
            while (off < nbytes && e < NB) {
                uint32_t chunk = 0x8000;
                if (off + chunk > nbytes)
                    chunk = nbytes - off;
                chunk &= ~3u;
                if (chunk == 0)
                    break;
                bdl[e].addr  = (uint32_t)(base + off);
                bdl[e].len   = (uint16_t)(chunk / 2);
                bdl[e].flags = 0x8000;
                off += chunk;
                e++;
            }
            if (e > 0)
                bdl[e - 1].flags = 0xc000;

            hal_outl(bm_base + 0x10, bdl_phys);
            hal_outb(bm_base + 0x15, (uint8_t)(e - 1));
            hal_outb(bm_base + 0x1b, 0x1d);

            serial_puts("[AC97] entries=");
            serial_puthex((uint32_t)e);
            serial_putc('\n');

            uint32_t t0 = timer_get_ticks();
            uint32_t deadline = samples / 480 + 400;
            ac_tick_wait(3);
            while (timer_get_ticks() - t0 < deadline) {
                if (hal_inb(bm_base + 0x16) & 0x01)
                    break;
                __asm__ volatile("pause");
            }
            ac_tick_wait(200);

            hal_outb(bm_base + 0x1b, 0x00);
            ac_tick_wait(30);
            serial_puts("[AC97] playback done\n");
        } else {
            serial_puts("[AC97] align overflow\n");
        }
    } else {
        serial_puts("[AC97] contiguous alloc failed\n");
    }

    for (uint32_t i = 0; i < alloc; i++)
        if (pg[i])
            pmm_free_page((void *)(uintptr_t)pg[i]);
    kfree(pg);
}

static uint8_t *load_asset(const char *fname, const char *payload_name,
                           uint32_t *outlen)
{
    if (fat_mounted()) {
        uint32_t sz = 0;
        if (fat_file_size(fname, &sz) == 0 && sz >= 4 &&
            sz <= 8u * 1024 * 1024) {
            uint8_t *buf = kmalloc((size_t)sz + 1);
            if (buf) {
                uint32_t got = 0;
                if (fat_read_file(fname, buf, sz + 1, &got) == 0 &&
                    got >= 4) {
                    *outlen = got;
                    return buf;
                }
            }
            if (buf)
                kfree(buf);
        }
    }
    const struct payload_mod *m = payload_find(payload_name);
    if (m && m->data && m->size >= 4) {
        *outlen = m->size;
        return (uint8_t *)m->data;
    }
    return 0;
}

void ac97_play_boot(void)
{
    uint32_t len = 0;
    uint8_t *d = load_asset("BOOT.PCM", "BOOTPCM", &len);
    if (d) {
        serial_puts("[AC97] play boot\n");
        ac97_play_pcm(d, len);
        if (fat_mounted())
            kfree(d);
    } else {
        serial_puts("[AC97] boot pcm missing\n");
    }
}

void ac97_play_warn(void)
{
    uint32_t len = 0;
    uint8_t *d = load_asset("WARN.PCM", "WARNPCM", &len);
    if (d) {
        ac97_play_pcm(d, len);
        if (fat_mounted())
            kfree(d);
    }
}

/* ---------------- synthesized cue tones ---------------- */

/* 256-entry integer sine table, amplitude 9000 (16-bit). */
static const int16_t sine256[256] = {
         0,    221,    442,    662,    882,   1102,   1321,   1539,
      1756,   1972,   2187,   2400,   2613,   2823,   3032,   3239,
      3444,   3647,   3848,   4047,   4243,   4436,   4627,   4815,
      5000,   5182,   5361,   5537,   5710,   5879,   6044,   6206,
      6364,   6518,   6669,   6815,   6957,   7095,   7229,   7358,
      7483,   7604,   7720,   7831,   7937,   8039,   8136,   8228,
      8315,   8397,   8474,   8546,   8612,   8674,   8730,   8781,
      8827,   8867,   8903,   8932,   8957,   8976,   8989,   8997,
      9000,   8997,   8989,   8976,   8957,   8932,   8903,   8867,
      8827,   8781,   8730,   8674,   8612,   8546,   8474,   8397,
      8315,   8228,   8136,   8039,   7937,   7831,   7720,   7604,
      7483,   7358,   7229,   7095,   6957,   6815,   6669,   6518,
      6364,   6206,   6044,   5879,   5710,   5537,   5361,   5182,
      5000,   4815,   4627,   4436,   4243,   4047,   3848,   3647,
      3444,   3239,   3032,   2823,   2613,   2400,   2187,   1972,
      1756,   1539,   1321,   1102,    882,    662,    442,    221,
         0,   -221,   -442,   -662,   -882,  -1102,  -1321,  -1539,
     -1756,  -1972,  -2187,  -2400,  -2613,  -2823,  -3032,  -3239,
     -3444,  -3647,  -3848,  -4047,  -4243,  -4436,  -4627,  -4815,
     -5000,  -5182,  -5361,  -5537,  -5710,  -5879,  -6044,  -6206,
     -6364,  -6518,  -6669,  -6815,  -6957,  -7095,  -7229,  -7358,
     -7483,  -7604,  -7720,  -7831,  -7937,  -8039,  -8136,  -8228,
     -8315,  -8397,  -8474,  -8546,  -8612,  -8674,  -8730,  -8781,
     -8827,  -8867,  -8903,  -8932,  -8957,  -8976,  -8989,  -8997,
     -9000,  -8997,  -8989,  -8976,  -8957,  -8932,  -8903,  -8867,
     -8827,  -8781,  -8730,  -8674,  -8612,  -8546,  -8474,  -8397,
     -8315,  -8228,  -8136,  -8039,  -7937,  -7831,  -7720,  -7604,
     -7483,  -7358,  -7229,  -7095,  -6957,  -6815,  -6669,  -6518,
     -6364,  -6206,  -6044,  -5879,  -5710,  -5537,  -5361,  -5182,
     -5000,  -4815,  -4627,  -4436,  -4243,  -4047,  -3848,  -3647,
     -3444,  -3239,  -3032,  -2823,  -2613,  -2400,  -2187,  -1972,
     -1756,  -1539,  -1321,  -1102,   -882,   -662,   -442,   -221,
};

void ac97_tone(int freq_hz, int ms)
{
    if (!present || freq_hz < 40 || freq_hz > 8000 || ms <= 0)
        return;
    if (ms > 800)
        ms = 800;
    uint32_t n = (uint32_t)AC97_RATE * (uint32_t)ms / 1000;
    int16_t *buf = kmalloc(n * 2);
    if (!buf)
        return;
    /* phase increment in 16.16 fixed point per sample */
    uint32_t step = (uint32_t)((uint64_t)freq_hz * 65536 / AC97_RATE);
    uint32_t phase = 0;
    uint32_t fade = AC97_RATE / 100;   /* 10 ms fade in/out */
    for (uint32_t i = 0; i < n; i++) {
        int s = sine256[(phase >> 16) & 255];
        phase += step;
        uint32_t g = 256;
        if (i < fade)
            g = i * 256 / fade;
        else if (i > n - fade)
            g = (n - i) * 256 / fade;
        buf[i] = (int16_t)(s * (int)g / 256);
    }
    ac97_play_pcm(buf, n * 2);
    kfree(buf);
}

/* ---------------- microphone capture (MC channel) ---------------- */

static int mic_on;
static uint32_t mic_bdl_phys;
static uint32_t mic_buf_phys;
static uint32_t mic_buf_bytes;

int ac97_mic_running(void) { return mic_on; }

int ac97_mic_init(void)
{
    if (!present)
        return 0;
    if (mic_on)
        return 1;

    /* mixer: select MIC as record source, unmute mic + gain */
    codecw(0x1a, 0x0000);   /* record select: microphone */
    codecw(0x1c, 0x0000);   /* record gain: max, unmuted */
    codecw(0x0e, 0x0080);   /* mic volume: 0 dB gain, unmute (bit15=0) */

    /* two contiguous pages for the ring buffer + a BDL page */
    void *bdl = pmm_alloc_page_below(0x40000000ull);
    void *pg0 = pmm_alloc_page_below(0x40000000ull);
    void *pg1 = pmm_alloc_page_below(0x40000000ull);
    if (!bdl || !pg0 || !pg1)
        return 0;
    memset(bdl, 0, PAGE);
    memset(pg0, 0, PAGE);
    memset(pg1, 0, PAGE);
    mic_bdl_phys = (uint32_t)(uintptr_t)bdl;
    mic_buf_phys = (uint32_t)(uintptr_t)pg0;
    mic_buf_bytes = PAGE * 2;

    struct bdl_entry *e = (void *)(uintptr_t)mic_bdl_phys;
    e[0].addr = mic_buf_phys;
    e[0].len = PAGE / 2;
    e[0].flags = 0x8000;
    e[1].addr = mic_buf_phys + PAGE;
    e[1].len = PAGE / 2;
    e[1].flags = 0x8000;

    hal_outb(bm_base + 0x2b, 0x02);   /* MC reset */
    ac_tick_wait(2);
    hal_outb(bm_base + 0x2b, 0x00);
    hal_outl(bm_base + 0x20, mic_bdl_phys);
    hal_outb(bm_base + 0x25, 1);      /* last valid index = 1 */
    hal_outb(bm_base + 0x2b, 0x0d);   /* run + interrupt enables */

    mic_on = 1;
    serial_puts("[AC97] mic capture started\n");
    return 1;
}

int ac97_mic_level(void)
{
    if (!mic_on)
        return -1;
    /* read the current controller index to sample fresh data */
    uint8_t civ = hal_inb(bm_base + 0x24);
    uint32_t off = (uint32_t)civ * PAGE;
    if (off >= mic_buf_bytes)
        off = 0;
    const int16_t *buf = (const void *)(uintptr_t)mic_buf_phys;
    uint32_t sample_off = off / 2;
    uint32_t total = 4096;
    uint64_t acc = 0;
    for (uint32_t i = 0; i < total; i++) {
        int16_t v = buf[(sample_off + i) % (mic_buf_bytes / 2)];
        acc += (uint32_t)(v * v);
    }
    int rms = (int)(acc / total);
    /* rms is squared amplitude; sqrt by rough table-free approximation */
    int s = 0;
    while ((s + 1) * (s + 1) <= rms)
        s++;
    /* map ~300..12000 amplitude to 0..100 */
    int lvl = (s - 300) * 100 / 11700;
    if (lvl < 0) lvl = 0;
    if (lvl > 100) lvl = 100;
    return lvl;
}
