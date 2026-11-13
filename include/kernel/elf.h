#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdint.h>

/* ELF64 (x86-64) loader. Only what our own toolchain emits is accepted:
   ELFCLASS64, little endian, ET_EXEC linked inside the user window, or
   ET_DYN rebased onto USER_BASE. No dynamic linking, no relocations yet -
   those arrive with the dlopen/loader milestone. */

enum {
    ELF_OK = 0,
    ELF_E_SHORT = 1,
    ELF_E_MAGIC = 2,
    ELF_E_CLASS = 3,
    ELF_E_ENDIAN = 4,
    ELF_E_MACHINE = 5,
    ELF_E_TYPE = 6,
    ELF_E_PHDR = 7,
    ELF_E_SEGMENT = 8,
    ELF_E_RANGE = 9
};

struct elf_info {
    uint64_t entry;
    uint64_t load_base;
    uint64_t image_bytes;
    int segments;
};

int elf_describe(const uint8_t *data, uint32_t len, struct elf_info *info);

#endif
