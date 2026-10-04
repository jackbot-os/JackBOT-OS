#ifndef JACKBOT_PERSIST_H
#define JACKBOT_PERSIST_H
#include <stdint.h>

#define PERSIST_NAME_MAX 32
#define PERSIST_MAX_DIRTY 32

void persist_init(void);
void persist_mark_dirty(const char *name);
int persist_flush(uint32_t timeout_ticks);
int persist_pending(void);
void persist_self_test(void);

#endif
