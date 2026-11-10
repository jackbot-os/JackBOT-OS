#ifndef KERNEL_CLOCK_H
#define KERNEL_CLOCK_H

#include <stdint.h>

/* One consistent CMOS RTC snapshot. */
struct rtc_time {
    int year, month, day;
    int hour, min, sec;
    /* what the chip reported, for the probe log */
    uint8_t hour12;
    uint8_t binary;
    uint8_t bcd;
};

void rtc_read(struct rtc_time *t);
/* RTC seconds + 10 ms sub-second steps from the PIT. */
void clock_now(struct rtc_time *t, int *ms);
void clock_init(void);
int weekday_of(int y, int m, int d);   /* 0 = Sunday */

#endif
