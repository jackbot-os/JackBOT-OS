#include "jb.h"

size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char *strcat(char *d, const char *s)
{
    char *r = d;
    while (*d) d++;
    while ((*d++ = *s++)) ;
    return r;
}

char *strchr(const char *s, int c)
{
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return c == 0 ? (char *)s : 0;
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char *)last;
}

char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) return (char *)h;
    for (; *h; h++)
        if (!strncmp(h, n, nl)) return (char *)h;
    return 0;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}

void *memset(void *d, int v, size_t n)
{
    unsigned char *dd = d;
    while (n--) *dd++ = (unsigned char)v;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}

int atoi(const char *s)
{
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v * sign;
}
