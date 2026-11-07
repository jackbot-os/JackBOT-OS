#include <stdint.h>
#include <stddef.h>
#include <kernel/dev.h>
#include <kernel/serial.h>
#include <kernel/kstring.h>

#define MAX_DRIVERS 256

static const struct dev_driver *g_drivers[MAX_DRIVERS];
static int g_ndrv;
static struct dev_instance g_inst[DEV_MAX];
static int g_ninst;

void dev_register(const struct dev_driver *drv)
{
    if (!drv || g_ndrv >= MAX_DRIVERS)
        return;
    g_drivers[g_ndrv++] = drv;
}

static int match(const struct dev_driver *drv, struct pci_dev *d)
{
    int c_ok = (drv->class_code == 0) ||
               (d->class_code == drv->class_code &&
                (drv->subclass == 0 || d->subclass == drv->subclass));
    int id_ok = (drv->vendor == 0) ||
                (d->vendor == drv->vendor && d->device == drv->device);
    return c_ok && id_ok;
}

static const char *class_name(uint8_t c)
{
    switch (c) {
    case 0x03: return "display";
    case 0x01: return "storage";
    case 0x02: return "net";
    case 0x04: return "audio";
    case 0x05: return "input";
    case 0x06: return "timer";
    case 0x0C: return "serial-bus";
    default:   return "other";
    }
}

int dev_probe_all(void)
{
    g_ninst = 0;
    int ok_count = 0;
    for (int i = 0; i < pci_count() && g_ninst < DEV_MAX; i++) {
        struct pci_dev *d = pci_get(i);
        const struct dev_driver *chosen = 0;
        for (int j = 0; j < g_ndrv; j++)
            if (match(g_drivers[j], d)) { chosen = g_drivers[j]; break; }
        struct dev_instance *in = &g_inst[g_ninst];
        in->pci = d;
        in->class = d->class_code;
        in->subclass = d->subclass;
        in->ok = 0;
        if (chosen) {
            in->drv = chosen;
            strncpy(in->name, chosen->name, DEV_NAME_MAX - 1);
            in->name[DEV_NAME_MAX - 1] = 0;
            in->ok = chosen->init(d) == 0;
            if (in->ok)
                ok_count++;
        } else {
            in->drv = 0;
            in->name[0] = 0;
        }
        serial_puts("[DEV] ");
        serial_puthex(d->vendor); serial_putc(':');
        serial_puthex(d->device);
        serial_puts(" class="); serial_puthex(d->class_code);
        serial_putc('/'); serial_puthex(d->subclass);
        serial_puts(" -> ");
        serial_puts(in->name[0] ? in->name : "(none)");
        serial_puts(in->ok ? " OK\n" : "\n");
        g_ninst++;
    }
    return ok_count;
}

struct dev_instance *dev_find(uint8_t class_code, int idx)
{
    int n = 0;
    for (int i = 0; i < g_ninst; i++)
        if (g_inst[i].class == class_code) {
            if (n == idx)
                return &g_inst[i];
            n++;
        }
    return 0;
}

int dev_count(uint8_t class_code)
{
    int n = 0;
    for (int i = 0; i < g_ninst; i++)
        if (g_inst[i].class == class_code)
            n++;
    return n;
}

int dev_driver_count(void) { return g_ndrv; }
int dev_instance_count(void) { return g_ninst; }

void dev_print_all(void)
{
    serial_puts("[DEV] device tree:\n");
    for (int i = 0; i < g_ninst; i++) {
        struct dev_instance *in = &g_inst[i];
        serial_puts("  #");
        serial_puthex((uint32_t)i);
        serial_puts(" ");
        serial_puts(class_name(in->class));
        serial_puts(" ");
        serial_puts(in->name[0] ? in->name : "?");
        serial_puts(in->ok ? " [OK]" : " [idle]");
        serial_putc('\n');
    }
}
