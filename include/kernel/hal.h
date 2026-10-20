#ifndef JACKBOT_HAL_H
#define JACKBOT_HAL_H
/*
 * JackBOT OS Hardware Abstraction Layer (HAL)
 * 硬件兼容层：把 CPU 架构相关的硬件操作统一抽象，
 * 让内核主体（调度/文件/桌面/驱动框架）与 x86_64 / ARM64 等架构解耦。
 * 架构后端通过 JB_ARCH_X86_64 / JB_ARCH_ARM64 宏选择。
 */
#include <stdint.h>
#include <stddef.h>

/* ---- 架构标识 ---- */
#define JB_ARCH_UNKNOWN 0
#define JB_ARCH_X86_64  1
#define JB_ARCH_ARM64   2

uint32_t hal_arch_id(void);
const char *hal_arch_name(void);

/* ---- 早期初始化：串口 + 基础探测，在正式子系统之前调用 ---- */
void hal_early_init(void);

/* ---- I/O 端口（x86 风格；ARM 上通常无端口，映射为内存操作） ---- */
void     hal_outb(uint16_t port, uint8_t v);
void     hal_outw(uint16_t port, uint16_t v);
void     hal_outl(uint16_t port, uint32_t v);
uint8_t  hal_inb(uint16_t port);
uint16_t hal_inw(uint16_t port);
uint32_t hal_inl(uint16_t port);

/* ---- 内存映射 I/O（两架构通用） ---- */
void     hal_write8(volatile void *p, uint8_t v);
void     hal_write16(volatile void *p, uint16_t v);
void     hal_write32(volatile void *p, uint32_t v);
void     hal_write64(volatile void *p, uint64_t v);
uint8_t  hal_read8(volatile const void *p);
uint16_t hal_read16(volatile const void *p);
uint32_t hal_read32(volatile const void *p);
uint64_t hal_read64(volatile const void *p);

/* ---- 内存屏障 ---- */
void hal_membar(void);      /* 读写全屏障 */
void hal_membar_read(void); /* 读屏障 */
void hal_membar_write(void);/* 写屏障 */

/* ---- 中断开关 ---- */
void hal_cli(void);
void hal_sti(void);
int  hal_int_state(void);   /* 返回 1 = 中断开启 */

/* ---- 定时器（PIT / 通用周期时钟） ---- */
void hal_timer_init(uint32_t hz);
uint64_t hal_timer_ticks(void);
void hal_timer_sleep_ms(uint32_t ms);

/* ---- 调试输出（引导早期 fallback 到串口，桌面起来后可重定向） ---- */
void hal_dbg_putc(char c);
void hal_dbg_puts(const char *s);
void hal_dbg_hex(uint64_t v);

/* ---- 架构自检 ---- */
int  hal_selftest(void);

#endif
