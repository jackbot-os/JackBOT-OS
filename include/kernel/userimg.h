#ifndef KERNEL_USERIMG_H
#define KERNEL_USERIMG_H

#include <stdint.h>
#include <kernel/proc.h>

/* Ring-3 image layout. Every user process lives in this window, so both the
   flat-image loader and the ELF loader agree on where code and stack go. */
#define USER_BASE 0x8000000000ull
#define USER_WINDOW_SIZE 0x4000000ull /* 64 MiB of VA per process */
#define USER_STACK_VADDR (USER_BASE + 0x100000)
#define USER_STACK_TOP (USER_STACK_VADDR + 0x1000)

/* Fresh top-level page table: a copy of the kernel's, so kernel mappings
   stay visible and ring 3 only sees what we add with PTE_USER. */
uint64_t user_p4_new(void);
/* Map one page into p4phys at vaddr (page aligned), copying n bytes from src
   at byte offset off inside the page; the rest of the page is zeroed. This is
   what lets a PT_LOAD segment that does not start on a page boundary land in
   the right place. Returns 0 on success, -1 when the frame could not be
   allocated. */
int user_map_page_from(uint64_t p4phys, uint64_t vaddr, const uint8_t *src,
                       uint64_t off, uint64_t n, uint64_t flags);
/* Map the standard 4 KiB user stack (RW, NX). Returns 0 on success. */
int user_stack_map(uint64_t p4phys);

/* Flat-machine-code images (ustest/uthread/...) */
thread_t *spawn_user_image(const uint8_t *code, uint64_t len);
/* ELF64 executable images linked into the USER_BASE window */
thread_t *spawn_user_elf(const uint8_t *data, uint32_t len);

#endif
