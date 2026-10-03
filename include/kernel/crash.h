#ifndef JACKBOT_CRASH_H
#define JACKBOT_CRASH_H

#include <stdint.h>
#include <stddef.h>
#include <kernel/trap.h>

#define CRASH_LOG_ENTRIES 16

struct crash_record {
    uint64_t used;
    uint64_t pid;
    uint64_t vector;
    uint64_t error;
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
    uint64_t regs[15];
    uint64_t stack_words[16];
};

void crash_log_frame(struct trap_frame *f, const char *note);
void crash_dump_records(void);
int user_ptr_valid(const void *p, uint64_t len);
void watchdog_init(void);

#endif
