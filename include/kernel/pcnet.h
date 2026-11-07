#ifndef JACKBOT_PCNET_H
#define JACKBOT_PCNET_H

#include <stdint.h>
#include <kernel/pci.h>

/* AMD PCnet-PCI II / FAST III (Am79C970A/973/975) NIC driver.
   Registered through the generic nic_ops layer (kernel/nic.c). */

int pcnet_init(void);
void pcnet_register_nic(void);

#endif
