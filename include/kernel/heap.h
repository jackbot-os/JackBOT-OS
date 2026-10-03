#ifndef JACKBOT_HEAP_H
#define JACKBOT_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t size);
uint64_t heap_bytes(void);

#endif
