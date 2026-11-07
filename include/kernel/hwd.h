#ifndef JACKBOT_HWD_H
#define JACKBOT_HWD_H
#include <kernel/pci.h>

/* NVMe controller probe: reads CAP/VS/CSTS and, when firmware left valid
   admin queues, performs the CC.EN -> CSTS.RDY handshake. */
int hwd_nvme_probe(struct pci_dev *d);
/* Intel HD Audio: GCAP/VMIN/STATESTS/GCTL from BAR0. */
int hwd_hda_probe(struct pci_dev *d);
/* xHCI: CAPLENGTH/HCIVERSION/HCSPARAMS/HCCPARAMS/DBOFF/RTSOFF. */
int hwd_xhci_probe(struct pci_dev *d);
/* Runs the three probes for whatever this machine actually exposes. */
void hwd_self_test(void);

#endif
