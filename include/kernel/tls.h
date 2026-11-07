#ifndef JACKBOT_TLS_H
#define JACKBOT_TLS_H

#include <stdint.h>
#include <stddef.h>

#define TLS_DEFAULT_PORT 443
#define TLS_RECORD_TYPE_HANDSHAKE 0x16
#define TLS_RECORD_TYPE_ALERT     0x15
#define TLS_RECORD_TYPE_APP_DATA  0x17
#define TLS_VERSION_1_2 0x0303

enum {
    TLS_IDLE = 0,
    TLS_CONNECTING,
    TLS_HANDSHAKE,
    TLS_ESTABLISHED,
    TLS_FAILED,
    TLS_UNAVAILABLE
};

int tls_engine_ready(void);
int tls_connect(int sock, const char *host);
int tls_send(int s, const void *data, uint16_t len);
int tls_recv(int s, void *buf, uint16_t cap);
void tls_close(int s);
int tls_state(void);
const char *tls_state_name(int state);

#endif
