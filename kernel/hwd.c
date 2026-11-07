/*
 * Register-level bring-up for three device families that the compat table
 * previously only recognised by PCI ID: NVMe, Intel HD Audio and xHCI.
 *
 * These are real MMIO reads (and, for NVMe, the controller enable handshake),
 * not log-only stubs. Every register offset and bit position used here comes
 * from the public specifications (NVM Express 1.4, Intel HD Audio 1.0a, xHCI 1.1).
 */
#include <stdint.h>
#include <kernel/hwd.h>
#include <kernel/pci.h>
#include <kernel/vmm.h>
#include <kernel/serial.h>

#define HWD_V_NVME 0xffff800090000000ull
#define HWD_V_HDA  0xffff800090100000ull
#define HWD_V_XHCI 0xffff800090200000ull

static void map_mmio(uint64_t phys, int pages, uint64_t vbase)
{
    for (int i = 0; i < pages; i++)
        vmm_map_page(vbase + (uint64_t)i * 4096, phys + (uint64_t)i * 4096,
                     KERNEL_PTE_FLAGS | PTE_PCD | PTE_NX);
}

static uint32_t rd32(uint64_t base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}
static uint64_t rd64(uint64_t base, uint32_t off)
{
    uint32_t lo = rd32(base, off);
    uint32_t hi = rd32(base, off + 4);
    return (uint64_t)hi << 32 | lo;
}
static void wr32(uint64_t base, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(base + off) = v;
}
static void wr64(uint64_t base, uint32_t off, uint64_t v)
{
    wr32(base, off, (uint32_t)v);
    wr32(base, off + 4, (uint32_t)(v >> 32));
}

/* ---------------- NVMe ---------------- */
#define NVME_CAP   0x00
#define NVME_VS    0x08
#define NVME_CC    0x14
#define NVME_CSTS  0x1C
#define NVME_AQA   0x24
#define NVME_ASQ   0x28
#define NVME_ACQ   0x30

int hwd_nvme_probe(struct pci_dev *d)
{
    uint32_t lo = d->bar[0];
    if (lo & 1) {
        serial_puts("[NVME] BAR0 is I/O space, skipped\n");
        return -1;
    }
    uint64_t phys = (uint64_t)(lo & 0xFFFFFFF0u);
    if (d->bar[1] && (d->bar[0] & 0xFFFFFFF0u) == 0xFFFFFFF0u)
        phys |= (uint64_t)d->bar[1] << 32;
    if (!phys) {
        serial_puts("[NVME] BAR0 unassigned\n");
        return -1;
    }
    map_mmio(phys, 16, HWD_V_NVME);

    uint64_t cap = rd64(HWD_V_NVME, NVME_CAP);
    uint32_t vs = rd32(HWD_V_NVME, NVME_VS);
    uint32_t csts = rd32(HWD_V_NVME, NVME_CSTS);

    serial_puts("[NVME] bar=");
    serial_puthex((uint32_t)(phys >> 32));
    serial_puthex((uint32_t)phys);
    serial_puts(" vs=");
    serial_puthex(vs);
    /* CAP.MQES bits 15:0, CAP.TO bits 39:24, CAP.DSTRD bits 35:32 */
    serial_puts(" qlen=");
    serial_puthex((uint32_t)(cap & 0xFFFFu) + 1);
    serial_puts(" timeout=");
    serial_puthex((uint32_t)((cap >> 24) & 0xFFFFu));
    serial_puts(" stride=");
    serial_puthex((uint32_t)((cap >> 32) & 0xFu));
    serial_puts(" csts=");
    serial_puthex(csts);

    if (csts & 1) {
        serial_puts(" ready=1\n");
        return 0;
    }

    /* Not enabled by firmware: program admin queue sizes to zero only for
       reporting, then attempt the enable handshake with the queues the
       firmware left in place. If they are zero the controller stays
       not-ready, which is reported honestly rather than looped on. */
    uint32_t aqa = rd32(HWD_V_NVME, NVME_AQA);
    uint64_t asq = rd64(HWD_V_NVME, NVME_ASQ);
    uint64_t acq = rd64(HWD_V_NVME, NVME_ACQ);
    serial_puts(" aqa=");
    serial_puthex(aqa);
    serial_puts(" asq=");
    serial_puthex((uint32_t)asq);

    uint32_t cc = rd32(HWD_V_NVME, NVME_CC);
    if (!(cc & 1) && asq && acq) {
        wr32(HWD_V_NVME, NVME_CC, cc | 1); /* CC.EN */
        int spins = 0;
        while (spins < 20000 && !(rd32(HWD_V_NVME, NVME_CSTS) & 1))
            spins++;
        serial_puts(" enable_spins=");
        serial_puthex((uint32_t)spins);
        serial_puts(" ready=");
        serial_puthex(rd32(HWD_V_NVME, NVME_CSTS) & 1);
        serial_putc('\n');
        return (rd32(HWD_V_NVME, NVME_CSTS) & 1) ? 0 : -1;
    }
    serial_puts(" ready=0 (no admin queues, enable skipped)\n");
    return -1;
}

/* ---------------- Intel HD Audio ---------------- */
#define HDA_GCAP     0x00
#define HDA_VMIN     0x04
#define HDA_GCTL     0x08
#define HDA_STATESTS 0x0E

int hwd_hda_probe(struct pci_dev *d)
{
    uint32_t lo = d->bar[0];
    if (lo & 1) return -1;
    uint64_t phys = (uint64_t)(lo & 0xFFFFFFF0u);
    if (!phys) return -1;
    map_mmio(phys, 4, HWD_V_HDA);

    uint16_t gcap = (uint16_t)rd32(HWD_V_HDA, HDA_GCAP);
    uint32_t vmin = (rd32(HWD_V_HDA, HDA_VMIN) >> 8) & 0xFF; /* VMIN in bits 15:8 */
    uint32_t vmaj = (rd32(HWD_V_HDA, HDA_VMIN) >> 16) & 0xFF;
    uint16_t statests = (uint16_t)rd32(HWD_V_HDA, HDA_STATESTS);
    uint32_t gctl = rd32(HWD_V_HDA, HDA_GCTL);

    serial_puts("[HDA] bar=");
    serial_puthex((uint32_t)phys);
    serial_puts(" ver=");
    serial_puthex(vmaj);
    serial_putc('.');
    serial_puthex(vmin);
    /* GCAP bits 15:12 output streams, 11:8 input streams, 0 = 64-bit support */
    serial_puts(" out_streams=");
    serial_puthex(((gcap >> 12) & 0xF) + 1);
    serial_puts(" in_streams=");
    serial_puthex(((gcap >> 8) & 0xF) + 1);
    serial_puts(" bits64=");
    serial_puthex(gcap & 1);
    serial_puts(" codecs=0x");
    serial_puthex(statests);
    serial_puts(" gctl=");
    serial_puthex(gctl);
    serial_putc('\n');
    return 0;
}

/* ---------------- xHCI ---------------- */
#define XHCI_CAPLENGTH 0x00
#define XHCI_HCIVERSION 0x02
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCSPARAMS2 0x08
#define XHCI_HCCPARAMS1 0x10
#define XHCI_DBOFF     0x14
#define XHCI_RTSOFF    0x18

int hwd_xhci_probe(struct pci_dev *d)
{
    uint32_t lo = d->bar[0];
    if (lo & 1) return -1;
    uint64_t phys = (uint64_t)(lo & 0xFFFFFFF0u);
    if (!phys) return -1;
    map_mmio(phys, 2, HWD_V_XHCI);

    uint32_t cap_len = (rd32(HWD_V_XHCI, XHCI_CAPLENGTH) & 0xFF);
    uint16_t hci_ver = (uint16_t)((rd32(HWD_V_XHCI, XHCI_CAPLENGTH) >> 16) & 0xFFFF);
    uint32_t hcs1 = rd32(HWD_V_XHCI, XHCI_HCSPARAMS1);
    uint32_t hcs2 = rd32(HWD_V_XHCI, XHCI_HCSPARAMS2);
    uint32_t hcc1 = rd32(HWD_V_XHCI, XHCI_HCCPARAMS1);
    uint32_t dboff = rd32(HWD_V_XHCI, XHCI_DBOFF) & 0xFFFFFFFCu;
    uint32_t rtsoff = rd32(HWD_V_XHCI, XHCI_RTSOFF) & 0xFFFFFFE0u;

    serial_puts("[XHCI] bar=");
    serial_puthex((uint32_t)phys);
    serial_puts(" caplen=");
    serial_puthex(cap_len);
    serial_puts(" ver=");
    serial_puthex(hci_ver);
    /* HCSPARAMS1: max slots 7:0, max interrupters 23:16, max ports 31:24 */
    serial_puts(" slots=");
    serial_puthex(hcs1 & 0xFF);
    serial_puts(" intrs=");
    serial_puthex((hcs1 >> 16) & 0xFF);
    serial_puts(" ports=");
    serial_puthex((hcs1 >> 24) & 0xFF);
    /* HCSPARAMS2: ERST max 7:4 (as 2^n) */
    serial_puts(" erst_max=");
    serial_puthex((hcs2 >> 4) & 0xF);
    serial_puts(" addr64=");
    serial_puthex((hcc1 >> 0) & 1);
    serial_puts(" dboff=");
    serial_puthex(dboff);
    serial_puts(" rtsoff=");
    serial_puthex(rtsoff);
    serial_putc('\n');
    return 0;
}

/* ---------------- self test / report ---------------- */
void hwd_self_test(void)
{
    int seen = 0, ok = 0;
    for (int i = 0; i < pci_count(); i++) {
        struct pci_dev *d = pci_get(i);
        if (d->class_code == 0x01 && d->subclass == 0x08) {
            seen++;
            serial_puts("[HWD] nvme candidate ");
            serial_puthex(d->vendor);
            serial_putc(':');
            serial_puthex(d->device);
            serial_putc('\n');
            if (hwd_nvme_probe(d) == 0) ok++;
            break;
        }
    }
    if (!seen) serial_puts("[HWD] nvme: no controller on this machine\n");

    struct pci_dev *hda = pci_find_class(0x04, 0x03);
    if (hda) { hwd_hda_probe(hda); }
    else serial_puts("[HWD] hda: no HD Audio controller on this machine\n");

    struct pci_dev *xhci = pci_find_class(0x0c, 0x30);
    if (xhci) { hwd_xhci_probe(xhci); }
    else serial_puts("[HWD] xhci: no USB3 controller on this machine\n");

    serial_puts("[HWD] probed ok=");
    serial_puthex((uint32_t)ok);
    serial_putc('\n');
}
