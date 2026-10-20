#ifndef JACKBOT_HTTP_H
#define JACKBOT_HTTP_H

#include <stdint.h>
#include <stddef.h>

#define HCLIENTS 2

enum {
    H_IDLE = 0,
    H_DNS,
    H_CONNECT,
    H_SEND,
    H_RECV,
    H_DONE,
    H_FAIL
};

int http_get_url(int c, const char *url);
int http_get(int c, const char *host, const char *path, uint16_t port);
void http_poll_client(int c);
int http_state(int c);
int http_ok(int c);
int http_failed(int c);
int http_status(int c);
const uint8_t *http_body(int c);
int http_body_len(int c);
void http_close_client(int c);

#endif
