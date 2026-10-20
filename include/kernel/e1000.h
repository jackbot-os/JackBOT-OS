#ifndef JACKBOT_E1000_H
#define JACKBOT_E1000_H

#include <stdint.h>
#include <stddef.h>

int e1000_init(void);
int e1000_send(const void *data, uint16_t len);
int e1000_recv(void *buf, uint16_t cap);
void e1000_get_mac(uint8_t out[6]);
int e1000_present(void);

#endif
