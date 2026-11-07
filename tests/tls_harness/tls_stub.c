#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <kernel/tcp.h>
#include <kernel/net.h>

/* POSIX stubs for the kernel network/timer/serial functions used by tls.c. */

static int fd = -1;
static int peer_closed;
static int failed;

uint64_t timer_get_ticks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}

int tcp_connected(int s) { (void)s; return fd >= 0; }
int tcp_failed(int s) { (void)s; return failed; }
int tcp_peer_closed(int s) { (void)s; return peer_closed; }

int tcp_connect(uint32_t ip, uint16_t port) { (void)ip; (void)port; return 0; }
int tcp_close(int s) { (void)s; return 0; }
void tcp_poll(void) {}
void tcp_in(struct ip_hdr *ip, const uint8_t *payload, uint16_t len)
{
    (void)ip; (void)payload; (void)len;
}

/* The test driver opens the socket and passes the fd as the "socket id". */
void tls_test_set_fd(int f) { fd = f; }
int tls_test_get_fd(void) { return fd; }

int tcp_send(int s, const void *data, uint16_t len)
{
    fd = s;
    ssize_t n = send(fd, data, len, 0);
    if (n < 0) {
        failed = 1;
        return -1;
    }
    return (int)n;
}

int tcp_recv(int s, void *buf, uint16_t cap)
{
    fd = s;
    ssize_t n = recv(fd, buf, cap, MSG_DONTWAIT);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        failed = 1;
        return 0;
    }
    if (n == 0)
        peer_closed = 1;
    return (int)n;
}

void net_poll(void) {}
int net_ready(void) { return 1; }

/* Serial stubs (the kernel TLS client logs handshake failures there). */
void serial_puts(const char *s) { fputs(s, stdout); }
void serial_putc(char c) { putchar(c); }
void serial_puthex(uint64_t v) { printf("%016llx", (unsigned long long)v); }
