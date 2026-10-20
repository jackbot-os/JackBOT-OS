#include <stdint.h>
#include <stddef.h>
#include <kernel/nic.h>
#include <kernel/serial.h>

static const struct nic_ops *g_nics[NIC_MAX];
static int g_nnics;
static const struct nic_ops *g_active;

int nic_register(const struct nic_ops *ops)
{
    if (!ops || g_nnics >= NIC_MAX)
        return -1;
    g_nics[g_nnics++] = ops;
    serial_puts("[NIC] registered ");
    serial_puts(ops->name);
    serial_putc('\n');
    return 0;
}

int nic_init_all(void)
{
    g_active = 0;
    for (int i = 0; i < g_nnics; i++) {
        const struct nic_ops *ops = g_nics[i];
        struct pci_dev *d = 0;
        if (ops->init)
            d = 0; /* 驱动内自行 pci_find */
        if (ops->init && ops->init(d) == 0) {
            g_active = ops;
            serial_puts("[NIC] active = ");
            serial_puts(ops->name);
            serial_putc('\n');
            return 0;
        }
    }
    serial_puts("[NIC] no NIC available\n");
    return -1;
}

int nic_active(void) { return g_active != 0; }

int nic_send(const void *data, uint16_t len)
{
    if (!g_active || !g_active->send)
        return -1;
    return g_active->send(data, len);
}

int nic_recv(void *buf, uint16_t cap)
{
    if (!g_active || !g_active->recv)
        return 0;
    return g_active->recv(buf, cap);
}

void nic_get_mac(uint8_t out[6])
{
    if (g_active && g_active->get_mac)
        g_active->get_mac(out);
    else {
        for (int i = 0; i < 6; i++)
            out[i] = 0;
    }
}

const char *nic_active_name(void)
{
    return g_active ? g_active->name : "none";
}
