/*
 * Hardware compatibility layer: turns the raw PCI inventory into a machine
 * profile plus a per-class coverage report.
 *
 * The point is to answer "will this machine boot into a usable desktop" from
 * what the kernel can actually see: which hypervisor or chipset family we are
 * on, how many devices got a driver, and which classes still have no match.
 */
#include <stdint.h>
#include <kernel/hwcompat.h>
#include <kernel/dev.h>
#include <kernel/pci.h>
#include <kernel/serial.h>

static const char *g_profile = "generic";
static const char *g_vendor = "unknown";
static const char *g_hypervisor = "none";

static int has_id(uint16_t vendor, uint16_t device)
{
    for (int i = 0; i < pci_count(); i++) {
        struct pci_dev *d = pci_get(i);
        if (d->vendor == vendor && d->device == device)
            return 1;
    }
    return 0;
}

static int has_vendor(uint16_t vendor)
{
    for (int i = 0; i < pci_count(); i++)
        if (pci_get(i)->vendor == vendor)
            return 1;
    return 0;
}

const char *hwcompat_hypervisor(void) { return g_hypervisor; }
const char *hwcompat_profile(void) { return g_profile; }

void hwcompat_detect(void)
{
    /* Virtual machines each leave a signature display or paravirt device */
    if (has_id(0x80ee, 0xbeef) || has_id(0x80ee, 0xcafe))
        g_hypervisor = "virtualbox";
    else if (has_id(0x1234, 0x1111))
        g_hypervisor = "qemu-bochs";
    else if (has_vendor(0x15ad))
        g_hypervisor = "vmware";
    else if (has_vendor(0x1af4))
        g_hypervisor = "kvm-virtio";
    else
        g_hypervisor = "none";

    if (has_vendor(0x8086))
        g_vendor = "intel";
    else if (has_vendor(0x1022))
        g_vendor = "amd";
    else if (has_vendor(0x1106))
        g_vendor = "via";
    else
        g_vendor = "generic";

    if (g_hypervisor[0] == 'v' && g_hypervisor[1] == 'i')
        g_profile = "virtualbox-guest";
    else if (g_hypervisor[0] == 'q')
        g_profile = "qemu-guest";
    else if (g_hypervisor[0] == 'v')
        g_profile = "vmware-guest";
    else if (g_vendor[0] == 'i')
        g_profile = "intel-platform";
    else if (g_vendor[0] == 'a')
        g_profile = "amd-platform";
    else
        g_profile = "generic-x86_64";

    serial_puts("[HW] profile=");
    serial_puts(g_profile);
    serial_puts(" chipset=");
    serial_puts(g_vendor);
    serial_puts(" hypervisor=");
    serial_puts(g_hypervisor);
    serial_putc('\n');
}

static const char *class_name(uint8_t c)
{
    switch (c) {
    case 0x01: return "storage";
    case 0x02: return "net";
    case 0x03: return "display";
    case 0x04: return "audio";
    case 0x05: return "input";
    case 0x06: return "bridge";
    case 0x0C: return "usb";
    default:   return "other";
    }
}

/* One line per class: matched instances / total instances on this machine.
   A class with 0 devices prints "absent" instead of looking broken. */
void hwcompat_report(void)
{
    static const uint8_t classes[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x0C };
    int total_ok = 0, total_bad = 0;

    serial_puts("[HW] drivers=");
    serial_puthex((uint32_t)dev_driver_count());
    serial_puts(" instances=");
    serial_puthex((uint32_t)dev_instance_count());
    serial_putc('\n');

    for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        int n = dev_count(classes[i]);
        if (!n) {
            serial_puts("[HW]   ");
            serial_puts(class_name(classes[i]));
            serial_puts(": absent\n");
            continue;
        }
        int ok = 0;
        for (int k = 0; k < n; k++) {
            struct dev_instance *in = dev_find(classes[i], k);
            if (in && in->ok) ok++;
        }
        serial_puts("[HW]   ");
        serial_puts(class_name(classes[i]));
        serial_puts(": ");
        serial_puthex((uint32_t)ok);
        serial_putc('/');
        serial_puthex((uint32_t)n);
        serial_puts(ok == n ? " bound\n" : " partial\n");
        total_ok += ok;
        total_bad += n - ok;
    }

    serial_puts("[HW] compat=");
    serial_puts(total_bad == 0 ? "full" : (total_ok ? "partial" : "degraded"));
    serial_puts(" bound=");
    serial_puthex((uint32_t)total_ok);
    serial_puts(" unbound=");
    serial_puthex((uint32_t)total_bad);
    serial_putc('\n');

    /* Devices on this bus that no driver in the table claimed at all. */
    int claimed = 0;
    for (int i = 0; i < pci_count(); i++) {
        struct pci_dev *d = pci_get(i);
        for (int c = 0; c < 0x10; c++) {
            int n = dev_count((uint8_t)c);
            for (int k = 0; k < n; k++) {
                struct dev_instance *in = dev_find((uint8_t)c, k);
                if (in && in->pci == d) claimed++;
            }
        }
    }
    serial_puts("[HW] pci_devices=");
    serial_puthex((uint32_t)pci_count());
    serial_puts(" claimed=");
    serial_puthex((uint32_t)claimed);
    serial_putc('\n');
}

void hwcompat_self_test(void)
{
    hwcompat_detect();
    hwcompat_report();
}
