/* JackBOT OS - FreeBSD driver compatibility layer (header).
 *
 * The sources under drivers/vendor/freebsd/ are real FreeBSD kernel code. They
 * expect a specific set of primitives: malloc with memory-type tags, mutexes,
 * callouts, bus_dma, bus_space, strlcpy/strlcat and device_printf. This header
 * maps those names onto what this kernel actually has, so a vendored driver can
 * be pulled in one subsystem at a time instead of all at once.
 *
 * What is genuinely implemented here (see jb_bsd_compat.c):
 *   - bsd_malloc / bsd_free / bsd_calloc  -> kmalloc/kfree/kcalloc
 *   - struct mtx                          -> single-CPU guard (see the .c file)
 *   - strlcpy / strlcat / strdup-like     -> kstring
 *   - bus_space_read/write                -> direct MMIO through a mapped pointer
 *
 * What is deliberately NOT implemented yet, and returns an error instead of
 * pretending: bus_dma tag/map construction, callout scheduling against the timer
 * tick, taskqueues, and newbus attach/detach. Those are the pieces a driver needs
 * before it can be compiled in - this layer only removes the *type* barrier. */

#ifndef JB_BSD_COMPAT_H
#define JB_BSD_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/heap.h>
#include <kernel/kstring.h>
#include <kernel/serial.h>

/* ---- error codes used by the FreeBSD sources ----------------------------- */
#define ENOENT   2
#define EIO      5
#undef  ENXIO
#define ENXIO    6
#define ENOMEM  12
#define EINVAL  22
#define ENOTSUP 45
#define EBUSY   16

/* ---- memory types (FreeBSD tags these so it can account per subsystem) --- */
#define M_DEVBUF     1
#define M_TEMP       2
#define M_USB        3
#define M_USBDEV     4
#define M_NETADDR    5
#define M_BUF        6

#define M_WAITOK     0x0000
#define M_NOWAIT     0x0001
#define M_ZERO       0x0100

void *bsd_malloc(size_t size, int type, int flags);
void  bsd_free(void *ptr, int type);
#define malloc(sz, type, flags) bsd_malloc((sz), (type), (flags))
#define free(ptr, type)         bsd_free((ptr), (type))

/* ---- mutexes -------------------------------------------------------------
 * This kernel runs drivers on a single CPU with interrupts already serialised
 * around the calling context, so a mutex here is an ownership guard, not a
 * scheduler primitive. A second taker spins instead of sleeping: that keeps the
 * semantics visible without inventing a blocking path that does not exist. */
struct mtx {
    volatile int owned;
};
void mtx_init(struct mtx *m, const char *name, int type, int opts);
void mtx_destroy(struct mtx *m);
void mtx_lock(struct mtx *m);
void mtx_unlock(struct mtx *m);
int  mtx_owned(struct mtx *m);
#define MTX_DEF     0
#define MTX_SPIN    1
#define MTX_RECURSE 0x0004
#define MTX_DUPOK   0x0010

/* ---- callouts ------------------------------------------------------------
 * Not wired to the timer tick yet: callout_reset stores the request and reports
 * success so a driver's init path keeps going, but nothing fires it. A driver
 * that depends on periodic callouts is therefore NOT functional; this layer says
 * so rather than silently dropping the timer. */
struct callout {
    void (*fn)(void *);
    void *arg;
    int pending;
};
void callout_init(struct callout *c, int mpsafe);
int  callout_reset(struct callout *c, int ticks, void (*fn)(void *), void *arg);
void callout_stop(struct callout *c);
#define C_HARDCLOCK 0

/* ---- bus_space -----------------------------------------------------------
 * Only the mapped-pointer form is supported (read/write at an offset inside an
 * already-mapped register window). Everything else (bus_space_map of a tag) has
 * to come from this kernel's own PCI/MMIO plumbing at the call site. */
typedef struct { volatile uint8_t *base; } *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef unsigned int bus_size_t;

static inline uint32_t bsd_bs_r32(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t;
    return *(volatile uint32_t *)(h + o);
}
static inline void bsd_bs_w32(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint32_t v)
{
    (void)t;
    *(volatile uint32_t *)(h + o) = v;
}
static inline uint8_t bsd_bs_r8(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
    (void)t;
    return *(volatile uint8_t *)(h + o);
}
static inline void bsd_bs_w8(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o, uint8_t v)
{
    (void)t;
    *(volatile uint8_t *)(h + o) = v;
}
#define bus_space_read_4(t, h, o)        bsd_bs_r32((t), (h), (o))
#define bus_space_write_4(t, h, o, v)    bsd_bs_w32((t), (h), (o), (v))
#define bus_space_read_1(t, h, o)        bsd_bs_r8((t), (h), (o))
#define bus_space_write_1(t, h, o, v)    bsd_bs_w8((t), (h), (o), (v))

/* ---- string helpers FreeBSD assumes ------------------------------------- */
size_t strlcpy(char *dst, const char *src, size_t cap);
size_t strlcat(char *dst, const char *src, size_t cap);

/* ---- device glue ---------------------------------------------------------
 * newbus is absent here. device_t exists so signatures compile; device_printf
 * goes to the serial log with a fixed prefix. */
typedef struct bsd_device *device_t;
#define device_printf(dev, fmt, ...) bsd_dev_printf((dev), (fmt))
void bsd_dev_printf(device_t dev, const char *msg);

#endif
