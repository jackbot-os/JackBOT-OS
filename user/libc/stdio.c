#include "jb.h"
#include <stdarg.h>

static FILE f_out = { 1, 0, 0 };
static FILE f_err = { 2, 0, 0 };
FILE *stdout = &f_out;
FILE *stderr = &f_err;

static char *utoa(unsigned long v, char *end, int base, int upper)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = dig[v % (unsigned)base]; v /= (unsigned)base; }
    while (i) *--end = tmp[--i];
    return end;
}

static int format(char *out, size_t cap, const char *fmt, va_list ap);

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{
    return format(buf, cap, fmt, ap);
}

static int format(char *out, size_t cap, const char *fmt, va_list ap)
{
    size_t o = 0;
    while (*fmt) {
        if (*fmt != '%') {
            if (o + 1 < cap) out[o] = *fmt;
            o++; fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            if (o + 1 < cap) out[o] = '%';
            o++; fmt++;
            continue;
        }
        if (*fmt == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s) { if (o + 1 < cap) out[o] = *s; o++; s++; }
            fmt++;
        } else if (*fmt == 'c') {
            int c = va_arg(ap, int);
            if (o + 1 < cap) out[o] = (char)c;
            o++; fmt++;
        } else if (*fmt == 'd' || *fmt == 'i') {
            long v = va_arg(ap, int);
            char tmp[24];
            char *p = tmp + sizeof tmp;
            int neg = v < 0;
            unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
            p = utoa(u, p, 10, 0);
            if (neg) *--p = '-';
            while (p < tmp + sizeof tmp) { if (o + 1 < cap) out[o] = *p; o++; p++; }
            fmt++;
        } else if (*fmt == 'u') {
            unsigned long v = va_arg(ap, unsigned int);
            char tmp[24];
            char *p = utoa(v, tmp + sizeof tmp, 10, 0);
            while (p < tmp + sizeof tmp) { if (o + 1 < cap) out[o] = *p; o++; p++; }
            fmt++;
        } else if (*fmt == 'x' || *fmt == 'X') {
            unsigned long v = va_arg(ap, unsigned int);
            char tmp[24];
            char *p = utoa(v, tmp + sizeof tmp, 16, *fmt == 'X');
            while (p < tmp + sizeof tmp) { if (o + 1 < cap) out[o] = *p; o++; p++; }
            fmt++;
        } else if (*fmt == 'p') {
            unsigned long v = (unsigned long)va_arg(ap, void *);
            char tmp[24];
            char *p = utoa(v, tmp + sizeof tmp, 16, 0);
            if (o + 1 < cap) out[o] = '0'; o++;
            if (o + 1 < cap) out[o] = 'x'; o++;
            while (p < tmp + sizeof tmp) { if (o + 1 < cap) out[o] = *p; o++; p++; }
            fmt++;
        } else {
            if (o + 1 < cap) out[o] = '%';
            o++;
            if (o + 1 < cap) out[o] = *fmt;
            o++;
            if (*fmt) fmt++;
        }
    }
    if (cap) out[o < cap ? o : cap - 1] = 0;
    return (int)o;
}

static int emit(FILE *f, const char *s, int n)
{
    if (n <= 0) return 0;
    return (int)write(f->fd, s, (size_t)n);
}

int printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = format(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return emit(stdout, buf, n);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = format(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return emit(f, buf, n);
}

int snprintf(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = format(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c)
{
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int fputc(int c, FILE *f)
{
    char ch = (char)c;
    return emit(f, &ch, 1) == 1 ? c : -1;
}

int puts(const char *s)
{
    write(1, s, strlen(s));
    write(1, "\n", 1);
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    int flags = (mode && (mode[0] == 'w' || mode[0] == 'a')) ? O_WRONLY : O_RDONLY;
    int fd = open(path, flags);
    static FILE pool[8];
    static int used[8];
    int i;
    if (fd < 0) return 0;
    for (i = 0; i < 8; i++)
        if (!used[i]) {
            used[i] = 1;
            pool[i].fd = fd;
            pool[i].eof = 0;
            pool[i].err = 0;
            return &pool[i];
        }
    close(fd);
    return 0;
}

int fclose(FILE *f)
{
    if (!f) return -1;
    close(f->fd);
    f->fd = -1;
    return 0;
}

size_t fread(void *buf, size_t sz, size_t n, FILE *f)
{
    size_t want = sz * n;
    ssize_t got;
    if (!f || sz == 0 || n == 0) return 0;
    got = read(f->fd, buf, want);
    if (got <= 0) { f->eof = 1; return 0; }
    return (size_t)got / sz;
}

size_t fwrite(const void *buf, size_t sz, size_t n, FILE *f)
{
    size_t want = sz * n;
    ssize_t put;
    if (!f || sz == 0 || n == 0) return 0;
    put = write(f->fd, buf, want);
    if (put <= 0) { f->err = 1; return 0; }
    return (size_t)put / sz;
}

int fseek(FILE *f, long off, int whence)
{
    if (!f) return -1;
    return lseek(f->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f)
{
    if (!f) return -1;
    return (long)lseek(f->fd, 0, SEEK_CUR);
}

int fgetc(FILE *f)
{
    unsigned char c;
    if (!f) return -1;
    if (read(f->fd, &c, 1) != 1) { f->eof = 1; return -1; }
    return c;
}

