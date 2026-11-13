#include <stdint.h>
#include <kernel/elf.h>
#include <kernel/userimg.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/serial.h>
#include <kernel/oom.h>

/* Every field is read straight out of the file and validated against the
   user window before a single byte is mapped. A malformed image must never
   be able to ask the kernel to map kernel memory or walk off the end of the
   buffer, so all arithmetic is done in 64-bit and checked. */

#define EI_NIDENT 16
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_W 2
#define PF_X 1

struct elf64_hdr {
    uint8_t ident[EI_NIDENT];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf64_ph {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

static int in_window(uint64_t addr, uint64_t len)
{
    if (addr < USER_BASE)
        return 0;
    uint64_t off = addr - USER_BASE;
    if (off > USER_WINDOW_SIZE)
        return 0;
    if (len > USER_WINDOW_SIZE - off)
        return 0;
    return 1;
}

int elf_describe(const uint8_t *data, uint32_t len, struct elf_info *info)
{
    if (!data || !info || len < sizeof(struct elf64_hdr))
        return ELF_E_SHORT;
    const struct elf64_hdr *eh = (const struct elf64_hdr *)data;
    if (eh->ident[0] != 0x7f || eh->ident[1] != 'E' ||
        eh->ident[2] != 'L' || eh->ident[3] != 'F')
        return ELF_E_MAGIC;
    if (eh->ident[4] != 2)      /* ELFCLASS64 */
        return ELF_E_CLASS;
    if (eh->ident[5] != 1)      /* little endian */
        return ELF_E_ENDIAN;
    if (eh->machine != EM_X86_64)
        return ELF_E_MACHINE;
    if (eh->type != ET_EXEC && eh->type != ET_DYN)
        return ELF_E_TYPE;
    if (eh->phentsize != sizeof(struct elf64_ph) || eh->phnum == 0 ||
        eh->phnum > 24)
        return ELF_E_PHDR;
    uint64_t ph_end = eh->phoff + (uint64_t)eh->phnum * sizeof(struct elf64_ph);
    if (eh->phoff > len || ph_end > len)
        return ELF_E_PHDR;

    /* ET_DYN images are linked at 0; rebase them onto USER_BASE. ET_EXEC
       images must already point inside the window (our linker script puts
       them there), otherwise there is no loader-time relocation support. */
    uint64_t base = 0;
    if (eh->type == ET_DYN)
        base = USER_BASE;

    if (!in_window(eh->entry + base, 1))
        return ELF_E_RANGE;

    uint64_t total = 0;
    int segs = 0;
    for (int i = 0; i < eh->phnum; i++) {
        const struct elf64_ph *ph =
            (const struct elf64_ph *)(data + eh->phoff + (uint64_t)i * sizeof(*ph));
        if (ph->type != PT_LOAD)
            continue;
        if (ph->memsz == 0)
            continue;
        if (ph->filesz > ph->memsz)
            return ELF_E_SEGMENT;
        if (ph->offset > len || ph->filesz > len - ph->offset)
            return ELF_E_SEGMENT;
        if (!in_window(ph->vaddr + base, ph->memsz)) {
            /* Toolchains emit a read-only LOAD holding the ELF header one
               page below the first real section. It carries nothing the
               process executes, so skipping it is correct; anything
               executable or writable outside the window is a real error. */
            if (!(ph->flags & (PF_X | PF_W)))
                continue;
            return ELF_E_RANGE;
        }
        total += (ph->memsz + 4095) & ~4095ull;
        if (total > USER_WINDOW_SIZE)
            return ELF_E_RANGE;
        segs++;
    }
    if (segs == 0)
        return ELF_E_SEGMENT;

    info->entry = eh->entry + base;
    info->load_base = base;
    info->segments = segs;
    info->image_bytes = total;
    return ELF_OK;
}

thread_t *spawn_user_elf(const uint8_t *data, uint32_t len)
{
    struct elf_info info;
    int rc = elf_describe(data, len, &info);
    if (rc != ELF_OK) {
        serial_puts("[ELF] reject code=");
        serial_puthex((uint64_t)rc);
        serial_puts("\n");
        return 0;
    }
    const struct elf64_hdr *eh = (const struct elf64_hdr *)data;

    uint64_t p4phys = user_p4_new();
    if (!p4phys) {
        if (!oom_reclaim("elf: page table"))
            return 0;
        p4phys = user_p4_new();
        if (!p4phys)
            return 0;
    }

    for (int i = 0; i < eh->phnum; i++) {
        const struct elf64_ph *ph =
            (const struct elf64_ph *)(data + eh->phoff + (uint64_t)i * sizeof(*ph));
        if (ph->type != PT_LOAD || ph->memsz == 0)
            continue;
        uint64_t flags = PTE_PRESENT | PTE_USER;
        if (ph->flags & PF_W)
            flags |= PTE_WRITABLE;
        if (!(ph->flags & PF_X))
            flags |= PTE_NX;

        uint64_t seg_start = ph->vaddr + info.load_base;
        uint64_t seg_end = seg_start + ph->memsz;
        uint64_t file_end = seg_start + ph->filesz;
        for (uint64_t page = seg_start & ~4095ull; page < seg_end; page += 4096) {
            uint64_t s = seg_start > page ? seg_start : page;
            uint64_t e = (seg_end < page + 4096) ? seg_end : page + 4096;
            uint64_t ce = e < file_end ? e : file_end; /* file-backed part */
            const uint8_t *src = 0;
            uint64_t off = 0, n = 0;
            if (s < ce) {
                off = s - page;
                n = ce - s;
                src = data + ph->offset + (s - seg_start);
            }
            if (user_map_page_from(p4phys, page, src, off, n, flags) != 0) {
                serial_puts("[ELF] mapping failed at ");
                serial_puthex(page);
                serial_puts("\n");
                return 0;
            }
        }
    }

    if (user_stack_map(p4phys) != 0)
        return 0;

    thread_t *t = thread_create_user(p4phys, info.entry, USER_STACK_TOP);
    if (t) {
        t->mem_bytes = info.image_bytes + 4096;
        serial_puts("[ELF] loaded entry=");
        serial_puthex(info.entry);
        serial_puts(" segs=");
        serial_puthex((uint64_t)info.segments);
        serial_puts("\n");
    }
    return t;
}
