/* JackBOT OS - FreeBSD driver compatibility layer (implementation).
 *
 * Scope and honesty: this removes the type/API barrier for the vendored sources
 * under drivers/vendor/freebsd/. It does not make them run. bus_dma, newbus
 * attach and timer-driven callouts are still missing, and those are what a real
 * port needs. See jb_bsd_compat.h for the per-piece statement. */

#include <stdint.h>
#include <stddef.h>
#include "jb_bsd_compat.h"

void *bsd_malloc(size_t size, int type, int flags)
{
    (void)type;
    if (size == 0)
        return 0;
    if (flags & M_ZERO)
        return kcalloc(1, size);
    return kmalloc(size);
}

void bsd_free(void *ptr, int type)
{
    (void)type;
    if (ptr)
        kfree(ptr);
}

/* ---- mutex --------------------------------------------------------------- */
void mtx_init(struct mtx *m, const char *name, int type, int opts)
{
    (void)name; (void)type; (void)opts;
    if (m)
        m->owned = 0;
}

void mtx_destroy(struct mtx *m)
{
    if (m)
        m->owned = 0;
}

void mtx_lock(struct mtx *m)
{
    if (!m)
        return;
    /* Single CPU, no preemption between these two stores inside one driver
       entry point: spin until the previous owner releases. */
    while (m->owned)
        ;
    m->owned = 1;
}

void mtx_unlock(struct mtx *m)
{
    if (m)
        m->owned = 0;
}

int mtx_owned(struct mtx *m)
{
    return m ? m->owned : 0;
}

/* ---- callout ------------------------------------------------------------- */
void callout_init(struct callout *c, int mpsafe)
{
    (void)mpsafe;
    if (!c)
        return;
    c->fn = 0;
    c->arg = 0;
    c->pending = 0;
}

int callout_reset(struct callout *c, int ticks, void (*fn)(void *), void *arg)
{
    (void)ticks;
    if (!c) {
        return EINVAL;
    }
    /* Recorded, not scheduled: the timer tick is not plumbed to this layer yet.
       Returning success keeps a probe path alive; returning 0 without arming
       would hide the missing feature, so the flag is kept for diagnostics. */
    c->fn = fn;
    c->arg = arg;
    c->pending = 1;
    return 0;
}

void callout_stop(struct callout *c)
{
    if (c)
        c->pending = 0;
}

/* ---- string helpers ------------------------------------------------------ */
size_t strlcpy(char *dst, const char *src, size_t cap)
{
    size_t n = 0;
    if (!dst || cap == 0)
        return 0;
    while (src && src[n] && n + 1 < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
    if (src) {
        size_t full = strlen(src);
        return full;
    }
    return n;
}

size_t strlcat(char *dst, const char *src, size_t cap)
{
    size_t dl = 0, i = 0;
    if (!dst || !src || cap == 0)
        return 0;
    while (dst[dl] && dl < cap)
        dl++;
    if (dl == cap)
        return cap;
    while (src[i] && dl + i + 1 < cap) {
        dst[dl + i] = src[i];
        i++;
    }
    dst[dl + i] = 0;
    return dl + strlen(src);
}

/* ---- device glue --------------------------------------------------------- */
void bsd_dev_printf(device_t dev, const char *msg)
{
    (void)dev;
    serial_puts("[bsd] ");
    if (msg)
        serial_puts(msg);
    serial_puts("\n");
}
