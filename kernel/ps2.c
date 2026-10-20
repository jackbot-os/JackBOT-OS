#include <stdint.h>
#include <stddef.h>
#include <kernel/ps2.h>
#include <kernel/fb.h>

#define KBD_BUF 64
static char kbuf[KBD_BUF];
static int khead, ktail;

static int shift, caps, e0;

static int mx, my;
static int left_prev;
static int mouse_primed;
static int click_pending, click_x, click_y;
static int left_down;
static uint8_t cycle[3];
static int cyc_idx;

static uint8_t inb(uint16_t p)
{
    uint8_t v;
    __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p));
    return v;
}
static void outb(uint16_t p, uint8_t v)
{
    __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(p));
}

static void wait_write(void)
{
    int t = 100000;
    while ((inb(0x64) & 0x02) && t--)
        ;
}
static void wait_read(void)
{
    int t = 100000;
    while (!(inb(0x64) & 0x01) && t--)
        ;
}

static void ctrl(uint8_t c)
{
    wait_write();
    outb(0x64, c);
}
static void dev_write(uint8_t c)
{
    wait_write();
    outb(0x60, c);
}
static uint8_t dev_read(void)
{
    wait_read();
    return inb(0x60);
}

static void mouse_send(uint8_t c)
{
    ctrl(0xd4);
    dev_write(c);
    dev_read();
}

static const char map[128] = {
    [0x01] = 27, [0x0e] = '\b', [0x0f] = '\t',
    [0x1c] = '\n', [0x39] = ' ',
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0a]='9',[0x0b]='0',
    [0x0c]='-',[0x0d]='=',[0x1a]='[',[0x1b]=']',[0x2b]='\\',
    [0x27]=';',[0x28]='\'',[0x29]='`',[0x33]=',',[0x34]='.',[0x35]='/',
    [0x1e]='a',[0x30]='b',[0x2e]='c',[0x20]='d',[0x12]='e',[0x21]='f',
    [0x22]='g',[0x23]='h',[0x17]='i',[0x24]='j',[0x25]='k',[0x26]='l',
    [0x32]='m',[0x31]='n',[0x18]='o',[0x19]='p',[0x10]='q',[0x13]='r',
    [0x1f]='s',[0x14]='t',[0x16]='u',[0x2f]='v',[0x11]='w',[0x2d]='x',
    [0x15]='y',[0x2c]='z',
};

static const char smap[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0a]='(',[0x0b]=')',
    [0x0c]='_',[0x0d]='+',[0x1a]='{',[0x1b]='}',[0x2b]='|',
    [0x27]=':',[0x28]='"',[0x29]='~',[0x33]='<',[0x34]='>',[0x35]='?',
};

static void enqueue(char c)
{
    int n = (ktail + 1) % KBD_BUF;
    if (n != khead) {
        kbuf[ktail] = c;
        ktail = n;
    }
}

void ps2_kbd_irq(void)
{
    uint8_t sc = inb(0x60);
    if (e0) {
        e0 = 0;
        if (!(sc & 0x80)) {
            char k = 0;
            if (sc == 0x48) k = JB_KEY_UP;
            else if (sc == 0x50) k = JB_KEY_DOWN;
            else if (sc == 0x4b) k = JB_KEY_LEFT;
            else if (sc == 0x4d) k = JB_KEY_RIGHT;
            else if (sc == 0x5b || sc == 0x5c) k = JB_KEY_WIN;
            if (k)
                enqueue(k);
        }
        return;
    }
    if (sc == 0xe0) {
        e0 = 1;
        return;
    }
    if (sc & 0x80) {
        uint8_t make = sc & 0x7f;
        if (make == 0x2a || make == 0x36)
            shift = 0;
        return;
    }
    if (sc == 0x2a || sc == 0x36) {
        shift = 1;
        return;
    }
    if (sc == 0x3a) {
        caps ^= 1;
        return;
    }
    char c = 0;
    if (shift && smap[sc])
        c = smap[sc];
    else if (map[sc])
        c = map[sc];
    if (c) {
        int isletter = (c >= 'a' && c <= 'z');
        if (isletter && ((caps && !shift) || (shift && !caps)))
            c -= 32;
        enqueue(c);
    }
}

void ps2_mouse_irq(void)
{
    uint8_t st = inb(0x64);
    uint8_t d = inb(0x60);
    if (!(st & 0x20))
        return;
    if (cyc_idx == 0 && !(d & 0x08))
        return;
    cycle[cyc_idx++] = d;
    if (cyc_idx < 3)
        return;
    cyc_idx = 0;

    uint8_t flags = cycle[0];
    int dx = (int8_t)cycle[1];
    int dy = (int8_t)cycle[2];

    mx += dx;
    my -= dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (fb_present()) {
        if (mx >= (int)fb_width()) mx = (int)fb_width() - 1;
        if (my >= (int)fb_height()) my = (int)fb_height() - 1;
    }

    left_down = flags & 0x01;
    if (!mouse_primed) {
        left_prev = left_down;
        mouse_primed = 1;
    } else if (!left_down && left_prev) {
        click_pending = 1;
        click_x = mx;
        click_y = my;
    }
    left_prev = left_down;
}

int ps2_read_key(char *out)
{
    if (khead == ktail)
        return 0;
    *out = kbuf[khead];
    khead = (khead + 1) % KBD_BUF;
    return 1;
}

int ps2_consume_key(char want)
{
    int i = khead;
    while (i != ktail) {
        if (kbuf[i] == want) {
            int j = i;
            while (1) {
                int n = (j + 1) % KBD_BUF;
                if (n == ktail) {
                    kbuf[j] = 0;
                    break;
                }
                kbuf[j] = kbuf[n];
                j = n;
            }
            ktail = (ktail - 1 + KBD_BUF) % KBD_BUF;
            return 1;
        }
        i = (i + 1) % KBD_BUF;
    }
    return 0;
}

void ps2_mouse_xy(int *x, int *y)
{
    *x = mx;
    *y = my;
}

int ps2_mouse_click(int *x, int *y)
{
    if (!click_pending)
        return 0;
    click_pending = 0;
    *x = click_x;
    *y = click_y;
    return 1;
}

int ps2_mouse_down(void) { return left_down; }

/* Discard every input event accumulated before a GUI starts: the firmware
   and early boot emit spurious mouse packets whose button edge would
   otherwise be consumed as a click on the first wizard frame. */
void ps2_mouse_flush(void)
{
    click_pending = 0;
    mouse_primed = 0;
    left_prev = 0;
    left_down = 0;
    khead = 0;
    ktail = 0;
}

void ps2_init(void)
{
    ctrl(0xad);
    ctrl(0xa7);

    ctrl(0x20);
    uint8_t cfg = dev_read();
    cfg |= 0x02;
    cfg &= (uint8_t)~0x20;
    ctrl(0x60);
    dev_write(cfg);

    ctrl(0xa8);
    ctrl(0xae);

    mouse_send(0xf6);
    mouse_send(0xf4);

    mx = fb_present() ? (int)fb_width() / 2 : 512;
    my = fb_present() ? (int)fb_height() / 2 : 384;
}
