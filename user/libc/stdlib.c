#include "jb.h"

/* Heap: first-fit free list over the brk region. Block header keeps the
   payload size and the next free block. */
struct hdr {
    size_t size;         /* payload bytes */
    int used;
    struct hdr *next;
};

static struct hdr *head;
static unsigned char *brk_cur;

static int more(size_t need)
{
    unsigned char *want = brk_cur + need;
    long r = __jb_sys(SYS_BRK, (long)(want ? want : need), 0, 0);
    if (r <= 0) return -1;
    if ((unsigned char *)r < want) return -1;
    brk_cur = (unsigned char *)r;
    return 0;
}

void *malloc(size_t n)
{
    struct hdr *b;
    size_t need;

    if (n == 0) n = 1;
    n = (n + 15) & ~(size_t)15;

    for (b = head; b; b = b->next)
        if (!b->used && b->size >= n) { b->used = 1; return b + 1; }

    if (!brk_cur) {
        long r = __jb_sys(SYS_BRK, 0, 0, 0);
        if (r <= 0) return 0;
        brk_cur = (unsigned char *)r;
    }
    need = sizeof(struct hdr) + n;
    b = (struct hdr *)brk_cur;
    if (more(need) != 0) return 0;
    b->size = n;
    b->used = 1;
    b->next = head;
    head = b;
    return b + 1;
}

void free(void *p)
{
    struct hdr *b;
    if (!p) return;
    b = ((struct hdr *)p) - 1;
    b->used = 0;
}

void *calloc(size_t n, size_t sz)
{
    size_t total = n * sz;
    void *p;
    if (n && sz > (size_t)-1 / n) return 0;
    p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n)
{
    struct hdr *b;
    void *np;
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    b = ((struct hdr *)p) - 1;
    if (b->size >= n) return p;
    np = malloc(n);
    if (!np) return 0;
    memcpy(np, p, b->size);
    free(p);
    return np;
}

int abs(int v) { return v < 0 ? -v : v; }

void exit(int code)
{
    __jb_sys(2, code, 0, 0);   /* legacy exit */
    for (;;) ;
}
