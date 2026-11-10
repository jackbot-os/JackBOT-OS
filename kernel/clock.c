#include <stdint.h>
#include <kernel/clock.h>
#include <kernel/serial.h>

/* One shared CMOS RTC reader. The previous code had two copies: the taskbar
   handled update-in-progress and 12-hour mode, the Clock app did not, so the
   two could disagree by a minute or show a wrong hour on a binary-mode RTC.
   Everything now goes through here. */

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

uint64_t timer_get_ticks(void);   /* 100 Hz PIT counter, kernel/timer.c */

static uint8_t cmos_read(int reg)
{
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)reg), "Nd"((uint16_t)CMOS_ADDR));
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"((uint16_t)CMOS_DATA));
    return v;
}

static int rtc_uip(void) { return cmos_read(0x0a) & 0x80; }
static int bcd2bin(uint8_t v) { return (v & 0x0f) + ((v >> 4) * 10); }

/* Read 0x00..0x09 as one consistent snapshot. A read that starts while the
   chip is updating returns a mixture of old and new fields; that is exactly
   what made the clock lag or jump. */
static void rtc_snapshot(uint8_t *r)
{
    int guard = 0;
    while (rtc_uip() && guard++ < 2000000)
        ;
    for (int round = 0; round < 4; round++) {
        for (int i = 0; i <= 9; i++)
            r[i] = cmos_read(i);
        if (!rtc_uip())
            break;
    }
    if (rtc_uip()) {                 /* still mid-update: one more pass */
        for (int i = 0; i <= 9; i++)
            r[i] = cmos_read(i);
    }
}

void rtc_read(struct rtc_time *t)
{
    uint8_t r[10];
    rtc_snapshot(r);
    uint8_t sb = cmos_read(0x0b);
    int binary = (sb & 0x04) != 0;
    int h24 = (sb & 0x02) != 0;

    int sec = binary ? (r[0] & 0x7f) : bcd2bin((uint8_t)(r[0] & 0x7f));
    int min = binary ? (r[2] & 0x7f) : bcd2bin((uint8_t)(r[2] & 0x7f));
    int hour;
    if (binary) {
        hour = r[4] & 0x7f;
    } else {
        int pm = r[4] & 0x80;
        hour = bcd2bin((uint8_t)(r[4] & 0x7f));
        if (!h24) {
            if (pm && hour != 12) hour += 12;
            else if (!pm && hour == 12) hour = 0;
        }
    }
    if (hour == 24) hour = 0;

    int day = binary ? r[7] : bcd2bin(r[7]);
    int mon = binary ? r[8] : bcd2bin(r[8]);
    int yr2 = binary ? r[9] : bcd2bin(r[9]);
    int year = 2000 + yr2;
    if (year < 2000) year = 2000;

    if (sec > 59) sec = 59;
    if (min > 59) min = 59;
    if (mon < 1 || mon > 12) mon = 1;
    if (day < 1 || day > 31) day = 1;

    t->hour = hour;
    t->min = min;
    t->sec = sec;
    t->day = day;
    t->month = mon;
    t->year = year;
    t->hour12 = !h24;
    t->binary = (uint8_t)binary;
    t->bcd = (uint8_t)!binary;
}

/* Sub-second anchoring: PIT ticks are 10 ms apart, so the milliseconds we
   report are honest 10 ms steps, not fabricated precision. When the RTC
   second changes we re-anchor instead of accumulating drift. */
static int anchor_sec = -1;
static uint64_t anchor_tick;

void clock_now(struct rtc_time *t, int *ms)
{
    struct rtc_time r;
    uint64_t before = timer_get_ticks();
    rtc_read(&r);
    if (r.sec != anchor_sec) {
        anchor_sec = r.sec;
        anchor_tick = timer_get_ticks();
    }
    uint64_t d = timer_get_ticks() - anchor_tick;
    if (d > 99)
        d = 99;
    *t = r;
    if (ms)
        *ms = (int)(d * 10);         /* 0..990, always inside the reported second */
    (void)before;
}

void clock_init(void)
{
    struct rtc_time t;
    rtc_read(&t);
    serial_puts("[CLOCK] rtc=");
    serial_puthex((uint64_t)t.year);
    serial_putc('-');
    serial_puthex((uint64_t)t.month);
    serial_putc('-');
    serial_puthex((uint64_t)t.day);
    serial_putc(' ');
    serial_puthex((uint64_t)t.hour);
    serial_putc(':');
    serial_puthex((uint64_t)t.min);
    serial_putc(':');
    serial_puthex((uint64_t)t.sec);
    serial_puts(t.bcd ? " bcd" : " bin");
    serial_puts(t.hour12 ? " 12h\n" : " 24h\n");
}

int weekday_of(int y, int m, int d)
{
    static const int tab[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y--;
    int r = (y + y / 4 - y / 100 + y / 400 + tab[m - 1] + d) % 7;
    return r < 0 ? r + 7 : r;
}
