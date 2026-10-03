#include <kernel/apic.h>

#define LAPIC_DEFAULT_BASE 0xfee00000
#define MSR_APIC_BASE 0x1b
#define APIC_BASE_ENABLE (1u << 11)

#define REG_ID 0x020
#define REG_EOI 0x0b0
#define REG_SPURIOUS 0x0f0
#define REG_LVT_TIMER 0x320
#define REG_INITIAL_COUNT 0x380
#define REG_CURRENT_COUNT 0x390
#define REG_DIVIDE_CONFIG 0x3e0

#define TIMER_VECTOR 0x20
#define TIMER_PERIODIC (1u << 17)
#define TIMER_MASKED (1u << 16)

#define PIT_CH0 0x40
#define PIT_MODE 0x43
#define PIT_FREQ 1193182u

static volatile uint8_t *lapic;

static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t timer_interval;

static uint32_t apic_read(uint32_t reg)
{
    return *(volatile uint32_t *)(lapic + reg);
}

static void apic_write(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(lapic + reg) = val;
}

void apic_timer_reset(void)
{
    apic_write(REG_INITIAL_COUNT, timer_interval);
}

static void pit_wait_10ms(void)
{
    uint16_t count = PIT_FREQ / 100;

    outb(PIT_MODE, 0x30);
    outb(PIT_CH0, count & 0xff);
    outb(PIT_CH0, count >> 8);

    for (;;) {
        outb(PIT_MODE, 0x00);
        uint8_t lo = inb(PIT_CH0);
        uint8_t hi = inb(PIT_CH0);
        uint16_t current = (uint16_t)((hi << 8) | lo);
        if (current <= 10)
            return;
    }
}

void apic_init(void)
{
    uint64_t base_msr = rdmsr(MSR_APIC_BASE);
    wrmsr(MSR_APIC_BASE, base_msr | APIC_BASE_ENABLE);

    uintptr_t phys = base_msr & 0xfffff000;
    if (phys == 0)
        phys = LAPIC_DEFAULT_BASE;
    lapic = (volatile uint8_t *)phys;

    apic_write(REG_SPURIOUS, 0x100 | 0xff);
}

void apic_timer_init(void)
{
    apic_write(REG_DIVIDE_CONFIG, 0x3);
    apic_write(REG_LVT_TIMER, TIMER_VECTOR | TIMER_MASKED);
    apic_write(REG_INITIAL_COUNT, 0xffffffff);

    pit_wait_10ms();

    uint32_t remaining = apic_read(REG_CURRENT_COUNT);
    uint32_t ticks_10ms = 0xffffffff - remaining;
    timer_interval = ticks_10ms;

    apic_write(REG_LVT_TIMER, TIMER_VECTOR);
    apic_write(REG_INITIAL_COUNT, ticks_10ms);
}

void apic_eoi(void)
{
    apic_write(REG_EOI, 0);
}
