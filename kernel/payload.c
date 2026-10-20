#include <stdint.h>
#include <stddef.h>
#include <kernel/payload.h>
#include <kernel/multiboot2.h>
#include <kernel/serial.h>

#define MAX_MODS 20
#define NAME_MAX 24

static struct payload_mod mods[MAX_MODS];
static char names[MAX_MODS][NAME_MAX];
static int nmods;
static int live;

static int streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int contains(const char *hay, const char *needle)
{
    if (!*needle)
        return 1;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n)
            return 1;
    }
    return 0;
}

static void first_token(char *dst, const char *src)
{
    while (*src == ' ' || *src == '\t' || *src == '\n' || *src == '\r')
        src++;
    int i = 0;
    while (*src && *src != ' ' && *src != '\t' && *src != '\n' &&
           *src != '\r' && i < NAME_MAX - 1) {
        dst[i++] = *src++;
    }
    dst[i] = 0;
}

void payload_collect(uint64_t mb_info)
{
    nmods = 0;
    live = 0;
    struct mb2_info *mi = (struct mb2_info *)(uintptr_t)mb_info;
    struct mb2_tag *t = mb2_first_tag(mi);
    while (t->type != MB2_TAG_END) {
        if (t->type == MB2_TAG_CMDLINE) {
            struct mb2_tag_cmdline *c = (struct mb2_tag_cmdline *)t;
            if (contains(c->string, "jb.live=1"))
                live = 1;
        } else if (t->type == MB2_TAG_MODULE && nmods < MAX_MODS) {
            struct mb2_tag_module *m = (struct mb2_tag_module *)t;
            first_token(names[nmods], m->cmdline);
            struct payload_mod *pm = &mods[nmods];
            pm->name = names[nmods];
            pm->data = (const void *)(uintptr_t)m->mod_start;
            pm->size = m->mod_end - m->mod_start;
            nmods++;
        }
        t = mb2_next_tag(t);
    }
    serial_puts(live ? "[PAYLOAD] LIVE install mode modules="
                     : "[PAYLOAD] installed mode modules=");
    serial_puthex((uint32_t)nmods);
    serial_putc('\n');
}

const struct payload_mod *payload_find(const char *name)
{
    for (int i = 0; i < nmods; i++)
        if (streq(mods[i].name, name))
            return &mods[i];
    return 0;
}

int payload_live_mode(void) { return live; }
int payload_count(void) { return nmods; }
