#include <stdint.h>
#include <stddef.h>
#include <kernel/ipc.h>
#include <kernel/proc.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>
#include <kernel/kstring.h>
#include <kernel/userimg.h>

#define IPC_PIPE_MAX 12
#define IPC_PIPE_BUF 2048
#define IPC_SHM_MAX 8
#define IPC_SHM_MAX_PAGES 16
#define IPC_SEM_MAX 16
#define IPC_MQ_MAX 8
#define IPC_MQ_DEPTH 8
#define IPC_MQ_MSG_LEN 64
#define IPC_WAIT_SPINS 200000

struct ipc_pipe {
    int used;
    int readers;
    int writers;
    uint32_t head;
    uint32_t tail;
    uint8_t buf[IPC_PIPE_BUF];
};

struct ipc_shm {
    int used;
    int npages;
    uint64_t phys[IPC_SHM_MAX_PAGES];
};

struct ipc_sem {
    int used;
    int value;
};

struct ipc_mq {
    int used;
    int count;
    int head;
    uint32_t len[IPC_MQ_DEPTH];
    char msg[IPC_MQ_DEPTH][IPC_MQ_MSG_LEN];
};

static struct ipc_pipe g_pipes[IPC_PIPE_MAX];
static struct ipc_shm g_shm[IPC_SHM_MAX];
static struct ipc_sem g_sem[IPC_SEM_MAX];
static struct ipc_mq g_mq[IPC_MQ_MAX];

static void zero_bytes(void *p, uint64_t n)
{
    uint8_t *b = p;
    for (uint64_t i = 0; i < n; i++)
        b[i] = 0;
}

void ipc_init(void)
{
    zero_bytes(g_pipes, sizeof g_pipes);
    zero_bytes(g_shm, sizeof g_shm);
    zero_bytes(g_sem, sizeof g_sem);
    zero_bytes(g_mq, sizeof g_mq);
}

/* ---------------- pipes ---------------- */

static int pipe_slot(int fd)
{
    if (fd < IPC_FD_BASE || fd >= IPC_FD_LIMIT)
        return -1;
    return (fd - IPC_FD_BASE) / 2;
}

int ipc_fd_is_pipe(int fd)
{
    return pipe_slot(fd) >= 0;
}

int ipc_pipe_create(int *out)
{
    for (int i = 0; i < IPC_PIPE_MAX; i++) {
        struct ipc_pipe *p = &g_pipes[i];
        if (p->used)
            continue;
        zero_bytes(p->buf, IPC_PIPE_BUF);
        p->used = 1;
        p->readers = 1;
        p->writers = 1;
        p->head = 0;
        p->tail = 0;
        out[0] = IPC_FD_BASE + i * 2;
        out[1] = IPC_FD_BASE + i * 2 + 1;
        return 0;
    }
    return -1;
}

static int pipe_used_bytes(const struct ipc_pipe *p)
{
    return (int)((p->tail + IPC_PIPE_BUF - p->head) % IPC_PIPE_BUF);
}

int64_t ipc_pipe_write(int fd, const void *buf, uint32_t len)
{
    int slot = pipe_slot(fd);
    if (slot < 0 || ((fd - IPC_FD_BASE) & 1) == 0)
        return -1;                       /* this is the read end */
    struct ipc_pipe *p = &g_pipes[slot];
    if (!p->used)
        return -1;
    const uint8_t *src = buf;
    uint32_t done = 0;
    while (done < len) {
        if (pipe_used_bytes(p) >= IPC_PIPE_BUF - 1) {
            /* Reader has to drain first; give it the CPU. */
            int spins = IPC_WAIT_SPINS;
            while (pipe_used_bytes(p) >= IPC_PIPE_BUF - 1 && spins-- > 0)
                thread_yield();
            if (pipe_used_bytes(p) >= IPC_PIPE_BUF - 1)
                break;
        }
        p->buf[p->tail] = src[done];
        p->tail = (p->tail + 1) % IPC_PIPE_BUF;
        done++;
    }
    return (int64_t)done;
}

int64_t ipc_pipe_read(int fd, void *buf, uint32_t len)
{
    int slot = pipe_slot(fd);
    if (slot < 0 || ((fd - IPC_FD_BASE) & 1) != 0)
        return -1;                       /* this is the write end */
    struct ipc_pipe *p = &g_pipes[slot];
    if (!p->used)
        return -1;
    uint8_t *dst = buf;
    uint32_t done = 0;
    while (done < len) {
        if (pipe_used_bytes(p) == 0) {
            if (p->writers == 0)
                break;                   /* EOF, like a closed pipe */
            int spins = IPC_WAIT_SPINS;
            while (pipe_used_bytes(p) == 0 && p->writers > 0 && spins-- > 0)
                thread_yield();
            if (pipe_used_bytes(p) == 0)
                break;
        }
        dst[done] = p->buf[p->head];
        p->head = (p->head + 1) % IPC_PIPE_BUF;
        done++;
    }
    return (int64_t)done;
}

int ipc_pipe_close(int fd)
{
    int slot = pipe_slot(fd);
    if (slot < 0)
        return -1;
    struct ipc_pipe *p = &g_pipes[slot];
    if (!p->used)
        return -1;
    if (((fd - IPC_FD_BASE) & 1) == 0) {
        if (p->readers > 0)
            p->readers--;
    } else {
        if (p->writers > 0)
            p->writers--;
    }
    if (p->readers == 0 && p->writers == 0) {
        p->used = 0;
        zero_bytes(p->buf, IPC_PIPE_BUF);
    }
    return 0;
}

/* ---------------- shared memory ---------------- */

int ipc_shm_create(uint32_t size)
{
    if (size == 0)
        return -1;
    int npages = (int)((size + 4095u) / 4096u);
    if (npages > IPC_SHM_MAX_PAGES)
        return -1;
    for (int i = 0; i < IPC_SHM_MAX; i++) {
        if (g_shm[i].used)
            continue;
        struct ipc_shm *s = &g_shm[i];
        for (int pg = 0; pg < npages; pg++) {
            void *frame = pmm_alloc_page();
            if (!frame) {
                for (int back = 0; back < pg; back++)
                    pmm_free_page((void *)(uintptr_t)s->phys[back]);
                zero_bytes(s, sizeof *s);
                return -1;
            }
            zero_bytes(frame, 4096);
            s->phys[pg] = (uint64_t)(uintptr_t)frame;
        }
        s->npages = npages;
        s->used = 1;
        return i;
    }
    return -1;
}

int ipc_shm_pages(int handle)
{
    if (handle < 0 || handle >= IPC_SHM_MAX || !g_shm[handle].used)
        return -1;
    return g_shm[handle].npages;
}

uint64_t ipc_shm_phys(int handle, int page)
{
    if (handle < 0 || handle >= IPC_SHM_MAX || !g_shm[handle].used)
        return 0;
    if (page < 0 || page >= g_shm[handle].npages)
        return 0;
    return g_shm[handle].phys[page];
}

int ipc_shm_map(int handle, uint64_t p4phys, uint64_t vaddr)
{
    if (handle < 0 || handle >= IPC_SHM_MAX || !g_shm[handle].used)
        return -1;
    if (vaddr < USER_BASE || vaddr + (uint64_t)g_shm[handle].npages * 4096 >
            USER_BASE + USER_WINDOW_SIZE)
        return -1;
    if ((vaddr & 4095) != 0)
        return -1;
    for (int pg = 0; pg < g_shm[handle].npages; pg++)
        vmm_map_in(p4phys, vaddr + (uint64_t)pg * 4096, g_shm[handle].phys[pg],
                   PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_NX);
    return 0;
}

/* ---------------- semaphores ---------------- */

int ipc_sem_create(int value)
{
    if (value < 0)
        return -1;
    for (int i = 0; i < IPC_SEM_MAX; i++) {
        if (g_sem[i].used)
            continue;
        g_sem[i].used = 1;
        g_sem[i].value = value;
        return i;
    }
    return -1;
}

int ipc_sem_wait(int id)
{
    if (id < 0 || id >= IPC_SEM_MAX || !g_sem[id].used)
        return -1;
    struct ipc_sem *s = &g_sem[id];
    int spins = IPC_WAIT_SPINS;
    while (s->value <= 0 && spins-- > 0)
        thread_yield();
    if (s->value <= 0)
        return -1;                       /* would block forever */
    s->value--;
    return 0;
}

int ipc_sem_post(int id)
{
    if (id < 0 || id >= IPC_SEM_MAX || !g_sem[id].used)
        return -1;
    g_sem[id].value++;
    return 0;
}

int ipc_sem_value(int id)
{
    if (id < 0 || id >= IPC_SEM_MAX || !g_sem[id].used)
        return -1;
    return g_sem[id].value;
}

/* ---------------- message queues ---------------- */

int ipc_mq_create(void)
{
    for (int i = 0; i < IPC_MQ_MAX; i++) {
        if (g_mq[i].used)
            continue;
        zero_bytes(&g_mq[i], sizeof g_mq[i]);
        g_mq[i].used = 1;
        return i;
    }
    return -1;
}

int ipc_mq_send(int id, const void *msg, uint32_t len)
{
    if (id < 0 || id >= IPC_MQ_MAX || !g_mq[id].used)
        return -1;
    if (len > IPC_MQ_MSG_LEN)
        return -1;
    struct ipc_mq *q = &g_mq[id];
    if (q->count >= IPC_MQ_DEPTH)
        return -1;
    int slot = (q->head + q->count) % IPC_MQ_DEPTH;
    const uint8_t *src = msg;
    for (uint32_t i = 0; i < len; i++)
        q->msg[slot][i] = (char)src[i];
    q->len[slot] = len;
    q->count++;
    return 0;
}

int ipc_mq_recv(int id, void *msg, uint32_t len)
{
    if (id < 0 || id >= IPC_MQ_MAX || !g_mq[id].used)
        return -1;
    struct ipc_mq *q = &g_mq[id];
    if (q->count <= 0)
        return -1;
    uint32_t n = q->len[q->head];
    if (n > len)
        n = len;
    uint8_t *dst = msg;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = (uint8_t)q->msg[q->head][i];
    q->head = (q->head + 1) % IPC_MQ_DEPTH;
    q->count--;
    return (int)n;
}

int ipc_mq_count(int id)
{
    if (id < 0 || id >= IPC_MQ_MAX || !g_mq[id].used)
        return -1;
    return g_mq[id].count;
}

/* ---------------- boot selftest ---------------- */

static void put(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

static void put_num(uint64_t v)
{
    char b[20];
    int n = 0;
    if (v == 0) {
        serial_putc('0');
        return;
    }
    while (v && n < 19) {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n-- > 0)
        serial_putc(b[n]);
}

void ipc_selftest(void)
{
    /* pipe: 24 bytes in, 24 bytes back out, in order */
    {
        int fds[2] = { 0, 0 };
        char out[32];
        char in[32];
        for (int i = 0; i < 24; i++)
            out[i] = (char)('A' + i % 26);
        if (ipc_pipe_create(fds) != 0) {
            put("[IPC] pipe create failed\n");
        } else {
            int64_t w = ipc_pipe_write(fds[1], out, 24);
            int64_t r = ipc_pipe_read(fds[0], in, 24);
            int same = (w == 24 && r == 24);
            for (int i = 0; same && i < 24; i++)
                if (in[i] != out[i])
                    same = 0;
            put("[IPC] pipe fd=");
            put_num((uint64_t)fds[0]);
            put("/");
            put_num((uint64_t)fds[1]);
            put(" w=");
            put_num((uint64_t)w);
            put(" r=");
            put_num((uint64_t)r);
            put(same ? " OK\n" : " MISMATCH\n");
            ipc_pipe_close(fds[0]);
            ipc_pipe_close(fds[1]);
        }
    }

    /* shared memory: two pages, mapped into a fresh user address space,
       then read back through the kernel's own view of the same frames */
    {
        int h = ipc_shm_create(8192);
        if (h < 0) {
            put("[IPC] shm create failed\n");
        } else {
            uint64_t p4 = user_p4_new();
            uint64_t va = USER_BASE + 0x200000;
            int ok = 0;
            if (p4 && ipc_shm_map(h, p4, va) == 0) {
                uint8_t *page0 = (uint8_t *)(uintptr_t)ipc_shm_phys(h, 0);
                uint8_t *page1 = (uint8_t *)(uintptr_t)ipc_shm_phys(h, 1);
                if (page0 && page1) {
                    page0[0] = 'S';
                    page1[0] = 'H';
                    ok = (vmm_phys_of_in(p4, va) == ipc_shm_phys(h, 0) &&
                          vmm_phys_of_in(p4, va + 4096) == ipc_shm_phys(h, 1) &&
                          page0[0] == 'S' && page1[0] == 'H');
                }
            }
            put("[IPC] shm pages=");
            put_num((uint64_t)(h >= 0 ? ipc_shm_pages(h) : 0));
            put(" va=");
            serial_puthex(va);
            put(ok ? " alias OK\n" : " alias FAIL\n");
        }
    }

    /* semaphore: post twice, wait twice, value must land on 0 */
    {
        int s = ipc_sem_create(0);
        int ok = 0;
        if (s >= 0) {
            ipc_sem_post(s);
            ipc_sem_post(s);
            int a = ipc_sem_wait(s);
            int b = ipc_sem_wait(s);
            ok = (a == 0 && b == 0 && ipc_sem_value(s) == 0);
        }
        put("[IPC] sem ");
        put(ok ? "post/wait OK\n" : "post/wait FAIL\n");
    }

    /* message queue: two messages in, two out, FIFO order */
    {
        int q = ipc_mq_create();
        int ok = 0;
        if (q >= 0) {
            const char *m1 = "one";
            const char *m2 = "two";
            char got[8];
            int n1 = ipc_mq_recv(q, got, sizeof got);   /* empty first */
            int s1 = ipc_mq_send(q, m1, 3);
            int s2 = ipc_mq_send(q, m2, 3);
            int c = ipc_mq_count(q);
            int r1 = ipc_mq_recv(q, got, sizeof got);
            int first_ok = (r1 == 3 && got[0] == 'o' && got[1] == 'n' && got[2] == 'e');
            int r2 = ipc_mq_recv(q, got, sizeof got);
            int second_ok = (r2 == 3 && got[0] == 't' && got[1] == 'w' && got[2] == 'o');
            ok = (n1 < 0 && s1 == 0 && s2 == 0 && c == 2 && first_ok && second_ok);
        }
        put("[IPC] mq fifo ");
        put(ok ? "OK\n" : "FAIL\n");
    }
}
