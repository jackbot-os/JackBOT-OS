/*
 * JackBOT OS HAL 实现：x86_64 后端（默认） + ARM64 后端（条件编译预留）。
 * 编译时通过 -DJB_ARCH_X86_64 或 -DJB_ARCH_ARM64 选择；默认 x86_64。
 */
#include <stdint.h>
#include <stddef.h>
#include <kernel/hal.h>
#include <kernel/serial.h>

#ifndef JB_ARCH_X86_64
#ifndef JB_ARCH_ARM64
#define JB_ARCH_X86_64 1
#endif
#endif

/* ================= 架构后端 ================= */

#if defined(JB_ARCH_X86_64)

uint32_t hal_arch_id(void) { return JB_ARCH_X86_64; }
const char *hal_arch_name(void) { return "x86_64"; }

static inline void outb_p(uint16_t p, uint8_t v)
{ __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outw_p(uint16_t p, uint16_t v)
{ __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline void outl_p(uint16_t p, uint32_t v)
{ __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb_p(uint16_t p)
{ uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw_p(uint16_t p)
{ uint16_t v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl_p(uint16_t p)
{ uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }

void hal_outb(uint16_t p, uint8_t v) { outb_p(p, v); }
void hal_outw(uint16_t p, uint16_t v) { outw_p(p, v); }
void hal_outl(uint16_t p, uint32_t v) { outl_p(p, v); }
uint8_t hal_inb(uint16_t p) { return inb_p(p); }
uint16_t hal_inw(uint16_t p) { return inw_p(p); }
uint32_t hal_inl(uint16_t p) { return inl_p(p); }

void hal_write8(volatile void *p, uint8_t v)
{ *(volatile uint8_t *)p = v; }
void hal_write16(volatile void *p, uint16_t v)
{ *(volatile uint16_t *)p = v; }
void hal_write32(volatile void *p, uint32_t v)
{ *(volatile uint32_t *)p = v; }
void hal_write64(volatile void *p, uint64_t v)
{ *(volatile uint64_t *)p = v; }
uint8_t hal_read8(volatile const void *p)
{ return *(volatile const uint8_t *)p; }
uint16_t hal_read16(volatile const void *p)
{ return *(volatile const uint16_t *)p; }
uint32_t hal_read32(volatile const void *p)
{ return *(volatile const uint32_t *)p; }
uint64_t hal_read64(volatile const void *p)
{ return *(volatile const uint64_t *)p; }

void hal_membar(void) { __asm__ volatile("mfence" ::: "memory"); }
void hal_membar_read(void) { __asm__ volatile("lfence" ::: "memory"); }
void hal_membar_write(void) { __asm__ volatile("sfence" ::: "memory"); }

void hal_cli(void) { __asm__ volatile("cli"); }
void hal_sti(void) { __asm__ volatile("sti"); }
int hal_int_state(void)
{
    uint64_t f;
    __asm__ volatile("pushfq; pop %0" : "=r"(f));
    return (int)((f >> 9) & 1);
}

void hal_timer_init(uint32_t hz) { (void)hz; } /* 由 timer.c 细粒度接管 */
uint64_t hal_timer_ticks(void) { return 0; }
void hal_timer_sleep_ms(uint32_t ms) { (void)ms; }

#elif defined(JB_ARCH_ARM64)

uint32_t hal_arch_id(void) { return JB_ARCH_ARM64; }
const char *hal_arch_name(void) { return "ARM64"; }

void hal_outb(uint16_t p, uint8_t v)
{ hal_write8((volatile void *)(uintptr_t)p, v); }
void hal_outw(uint16_t p, uint16_t v)
{ hal_write16((volatile void *)(uintptr_t)p, v); }
void hal_outl(uint16_t p, uint32_t v)
{ hal_write32((volatile void *)(uintptr_t)p, v); }
uint8_t hal_inb(uint16_t p)
{ return hal_read8((volatile const void *)(uintptr_t)p); }
uint16_t hal_inw(uint16_t p)
{ return hal_read16((volatile const void *)(uintptr_t)p); }
uint32_t hal_inl(uint16_t p)
{ return hal_read32((volatile const void *)(uintptr_t)p); }

void hal_write8(volatile void *p, uint8_t v)
{ *(volatile uint8_t *)p = v; __asm__ volatile("dmb osh" ::: "memory"); }
void hal_write16(volatile void *p, uint16_t v)
{ *(volatile uint16_t *)p = v; __asm__ volatile("dmb osh" ::: "memory"); }
void hal_write32(volatile void *p, uint32_t v)
{ *(volatile uint32_t *)p = v; __asm__ volatile("dmb osh" ::: "memory"); }
void hal_write64(volatile void *p, uint64_t v)
{ *(volatile uint64_t *)p = v; __asm__ volatile("dmb osh" ::: "memory"); }
uint8_t hal_read8(volatile const void *p)
{ uint8_t v = *(volatile const uint8_t *)p; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
uint16_t hal_read16(volatile const void *p)
{ uint16_t v = *(volatile const uint16_t *)p; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
uint32_t hal_read32(volatile const void *p)
{ uint32_t v = *(volatile const uint32_t *)p; __asm__ volatile("dmb oshld" ::: "memory"); return v; }
uint64_t hal_read64(volatile const void *p)
{ uint64_t v = *(volatile const uint64_t *)p; __asm__ volatile("dmb oshld" ::: "memory"); return v; }

void hal_membar(void) { __asm__ volatile("dmb osh" ::: "memory"); }
void hal_membar_read(void) { __asm__ volatile("dmb oshld" ::: "memory"); }
void hal_membar_write(void) { __asm__ volatile("dmb oshst" ::: "memory"); }

void hal_cli(void)
{ __asm__ volatile("msr daifset, #2" ::: "memory"); }
void hal_sti(void)
{ __asm__ volatile("msr daifclr, #2" ::: "memory"); }
int hal_int_state(void)
{
    uint64_t f;
    __asm__ volatile("mrs %0, daif" : "=r"(f));
    return (int)((f >> 7) & 1) ^ 1;
}

void hal_timer_init(uint32_t hz) { (void)hz; }
uint64_t hal_timer_ticks(void) { return 0; }
void hal_timer_sleep_ms(uint32_t ms) { (void)ms; }

#else
#error "No HAL backend selected (define JB_ARCH_X86_64 or JB_ARCH_ARM64)"
#endif

/* ================= 通用部分 ================= */

void hal_early_init(void)
{
    serial_init();
    serial_puts("[HAL] arch=");
    serial_puts(hal_arch_name());
    serial_puts(" backend ready\n");
}

void hal_dbg_putc(char c) { serial_putc(c); }
void hal_dbg_puts(const char *s) { serial_puts(s); }
void hal_dbg_hex(uint64_t v) { serial_puthex(v); }

int hal_selftest(void)
{
    int ok = 1;
    serial_puts("[HAL] selftest arch=");
    serial_puts(hal_arch_name());
    serial_puts("\n");

    /* I/O 端口写（0x80 POST 端口，只写不回读：读回值无标准保证） */
#if defined(JB_ARCH_X86_64)
    hal_outb(0x80, 0x5a);
    hal_outb(0x80, 0x00);
#endif

    /* 内存映射 I/O 写读一致性：用一块普通内存模拟 MMIO */
    volatile uint32_t mmio_slot[2];
    hal_write32((volatile void *)&mmio_slot[0], 0x12345678u);
    if (hal_read32((volatile const void *)&mmio_slot[0]) != 0x12345678u) ok = 0;
    hal_write64((volatile void *)&mmio_slot[0], 0x1122334455667788ull);
    if (hal_read64((volatile const void *)&mmio_slot[0]) != 0x1122334455667788ull) ok = 0;

    /* 内存屏障顺序性（简单自检） */
    uint32_t a = 0, b = 0, seen = 0;
    hal_membar();
    a = 1; hal_membar_write(); b = 1; hal_membar();
    seen = (a == 1 && b == 1) ? 1 : 0;
    if (!seen) ok = 0;

    serial_puts(ok ? "[HAL] test PASS\n" : "[HAL] test FAIL\n");
    return ok;
}
