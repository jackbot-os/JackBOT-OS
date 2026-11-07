#include <kernel/kstring.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    /* Word-at-a-time copy: framebuffer blits move megabytes per frame, and
       a byte loop was the main cost that made the UI and the mouse feel
       sluggish. Handles unaligned buffers by copying a few head bytes. */
    while (n && ((uintptr_t)d & 7)) {
        *d++ = *s++;
        n--;
    }
    if (((uintptr_t)s & 7) == 0) {
        uint64_t *dw = (uint64_t *)d;
        const uint64_t *sw = (const uint64_t *)s;
        while (n >= 8) {
            *dw++ = *sw++;
            n -= 8;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memset(void *dst, int v, size_t n)
{
    uint8_t *d = dst;
    uint8_t b = (uint8_t)v;
    uint64_t pattern = (uint64_t)b * 0x0101010101010101ull;
    while (n && ((uintptr_t)d & 7)) {
        *d++ = b;
        n--;
    }
    {
        uint64_t *dw = (uint64_t *)d;
        while (n >= 8) {
            *dw++ = pattern;
            n -= 8;
        }
        d = (uint8_t *)dw;
    }
    while (n--)
        *d++ = b;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x - *y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *r = dst;
    while ((*dst++ = *src++))
        ;
    return r;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *r = dst;
    while (*dst)
        dst++;
    while ((*dst++ = *src++))
        ;
    return r;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : 0;
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    do {
        if (*s == (char)c)
            last = s;
    } while (*s++);
    return (char *)last;
}

char *strstr(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl)
        return (char *)hay;
    for (; *hay; hay++)
        if (!strncmp(hay, needle, nl))
            return (char *)hay;
    return 0;
}

int atoi(const char *s)
{
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v * sign;
}

int toupper(int c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

int tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}
