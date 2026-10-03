#include <stdint.h>

#define PIT_CH0 0x40
#define PIT_MODE 0x43
#define PIT_FREQ 1193182
#define TIMER_HZ 100

static volatile uint64_t ticks;

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void timer_init(void)
{
    uint32_t divisor = PIT_FREQ / TIMER_HZ;
    outb(PIT_MODE, 0x36);
    outb(PIT_CH0, divisor & 0xff);
    outb(PIT_CH0, (divisor >> 8) & 0xff);
    ticks = 0;
}

void timer_irq(void)
{
    ticks++;
}

uint64_t timer_get_ticks(void)
{
    return ticks;
}
