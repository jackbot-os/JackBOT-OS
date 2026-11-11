#include <stdint.h>
#include <kernel/session.h>
#include <kernel/fat32.h>
#include <kernel/serial.h>
#include <kernel/ui.h>
#include <kernel/clock.h>

static char buf[256];

static void put_str(char **p, const char *s)
{
    while (*s)
        *(*p)++ = *s++;
}

static void put_num(char **p, const char *key, int v)
{
    put_str(p, key);
    put_str(p, "=");
    char n[12];
    int i = 0;
    if (v < 0) {
        *(*p)++ = '-';
        v = -v;
    }
    if (!v)
        n[i++] = '0';
    while (v && i < 11) {
        n[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i)
        *(*p)++ = n[--i];
    put_str(p, "\n");
}

int session_save(void)
{
    struct rtc_time t;
    rtc_read(&t);

    char *p = buf;
    put_str(&p, "JBSESSION 1\n");
    put_num(&p, "ui_scale", ui_scale());
    put_num(&p, "glass", ui_glass());
    put_num(&p, "year", (int)t.year);
    put_num(&p, "month", (int)t.month);
    put_num(&p, "day", (int)t.day);
    put_num(&p, "hour", (int)t.hour);
    put_num(&p, "min", (int)t.min);
    put_str(&p, "end\n");

    int len = (int)(p - buf);
    if (fat_write_file("JBSESSN.TXT", buf, (uint32_t)len) < 0)
        return -1;
    if (!fat_exists("JBSESSN.TXT"))
        return -2;
    return len;
}

int session_load(char *out, int max)
{
    if (max < 1)
        return -1;
    out[0] = 0;
    if (!fat_exists("JBSESSN.TXT"))
        return -1;

    uint32_t n = 0;
    if (fat_read_file("JBSESSN.TXT", out, (uint32_t)(max - 1), &n) < 0)
        return -1;
    if (!n)
        return -1;
    out[n] = 0;
    return (int)n;
}
