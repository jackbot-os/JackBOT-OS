#include <kernel/tls.h>
#include <kernel/serial.h>
#include <kernel/kstring.h>

int tls_engine_ready(void)
{
    return 0;
}

int tls_connect(int sock, const char *host)
{
    (void)sock;
    serial_puts("[TLS] engine not implemented: cannot handshake with ");
    serial_puts(host ? host : "(host)");
    serial_putc('\n');
    return -1;
}

int tls_send(int s, const void *data, uint16_t len)
{
    (void)s; (void)data; (void)len;
    return -1;
}

int tls_recv(int s, void *buf, uint16_t cap)
{
    (void)s; (void)buf; (void)cap;
    return -1;
}

const char *tls_state_name(int state)
{
    switch (state) {
    case TLS_IDLE:         return "idle";
    case TLS_CONNECTING:   return "connecting";
    case TLS_HANDSHAKE:    return "handshake";
    case TLS_ESTABLISHED:  return "established";
    case TLS_FAILED:       return "failed";
    case TLS_UNAVAILABLE:  return "unavailable";
    }
    return "?";
}
