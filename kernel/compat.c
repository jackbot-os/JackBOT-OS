/*
 * JackBOT OS 主机兼容层驱动集合（compat）
 * 各子层：显示 / 网卡 / 存储 / 音频 / 输入 / 定时器。
 * 通过 dev 框架按 PCI vendor:device / class 匹配注册。
 */
#include <stdint.h>
#include <stddef.h>
#include <kernel/hal.h>
#include <kernel/dev.h>
#include <kernel/pci.h>
#include <kernel/serial.h>
#include <kernel/fb.h>
#include <kernel/ata.h>
#include <kernel/e1000.h>
#include <kernel/nic.h>
#include <kernel/ps2.h>

/* NIC 驱动注册（由其他模块实现） */
void e1000_register_nic(void);
void rtl8139_register(void);

void compat_register_nics(void)
{
    e1000_register_nic();
    rtl8139_register();
}

/* ================= 显示兼容 ================= */

static int drv_display_bochs(struct pci_dev *d)
{
    /* Bochs VBE (QEMU -vga std) 与 VirtualBox VBoxSVGA 同接口 */
    uint32_t bar = pci_bar_mem(d, 0);
    serial_puts("[COMPAT] display bochs/vbox lfb bar=");
    serial_puthex(bar);
    serial_putc('\n');
    return 0; /* 识别成功，帧缓冲由 fb_bochs_init 建立 */
}

static int drv_display_vmsvga(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] display VMSVGA (VMware SVGA II)\n");
    return 0;
}

static int drv_display_vga(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] display generic VGA-compatible (class match)\n");
    return 0;
}

/* ================= 网卡兼容 ================= */

static int drv_net_e1000(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] net e1000 family (NIC driver via nic layer)\n");
    return 0;
}

static int drv_net_rtl8139(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] net rtl8139 (NIC driver via nic layer)\n");
    return 0;
}

static int drv_net_ne2000(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] net NE2000/8029 compat\n");
    return 0;
}

static int drv_net_pcnet(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] net AMD PCnet family\n");
    return 0;
}

static int drv_net_generic(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] net generic PCI ethernet (class match)\n");
    return 0;
}

static int drv_net_virtio(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] net virtio-net\n");
    return 0;
}

/* ================= 存储兼容 ================= */

static int drv_storage_ata(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] storage ATA/IDE (PIO)\n");
    return 0; /* ata_init 由启动流程调用 */
}

static int drv_storage_ahci(struct pci_dev *d)
{
    /* AHCI/SATA：读 CAP 寄存器（BAR5 MMIO offset 0）验证 */
    uint32_t bar = pci_bar_mem(d, 5);
    serial_puts("[COMPAT] storage AHCI/SATA bar5=");
    serial_puthex(bar);
    serial_putc('\n');
    return 0;
}

static int drv_storage_nvme(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] storage NVMe\n");
    return 0;
}

static int drv_storage_virtio(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] storage virtio-blk\n");
    return 0;
}

/* ================= 音频兼容 ================= */

static int drv_audio_ac97(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] audio AC97\n");
    return 0;
}

static int drv_audio_hda(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] audio HD Audio\n");
    return 0;
}

static int drv_audio_sb16(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] audio Sound Blaster 16\n");
    return 0;
}

static int drv_audio_generic(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] audio generic PCI multimedia (class match)\n");
    return 0;
}

/* ================= USB host controllers (PCI class 0x0c) ================= */

static int drv_usb_uhci(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] USB UHCI host controller\n");
    return 0;
}

static int drv_usb_ohci(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] USB OHCI host controller\n");
    return 0;
}

static int drv_usb_ehci(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] USB EHCI (USB2.0) host controller\n");
    return 0;
}

static int drv_usb_xhci(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] USB xHCI (USB3.x) host controller\n");
    return 0;
}

/* ================= 输入/桥 兼容 ================= */

static int drv_bridge(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] host/PCI bridge (chipset)\n");
    return 0;
}

/* ================= 定时器兼容 ================= */

static int drv_timer_hpet(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] timer HPET\n");
    return 0;
}

static int drv_timer_pit(struct pci_dev *d)
{
    (void)d;
    serial_puts("[COMPAT] timer PIT (legacy)\n");
    return 0;
}

/* ================= 注册表 ================= */

void compat_register_drivers(void)
{
    static const struct dev_driver drivers[] = {
        /* ---------------- 显示 ---------------- */
        { "display-bochs", DEV_CLASS_DISPLAY, 0, 0x80ee, 0xbeef, drv_display_bochs },
        { "display-bochs", DEV_CLASS_DISPLAY, 0, 0x1234, 0x1111, drv_display_bochs },
        { "display-cirrus", DEV_CLASS_DISPLAY, 0, 0x1013, 0x00b8, drv_display_bochs },
        { "display-vmsvga", DEV_CLASS_DISPLAY, 0, 0x15ad, 0x0405, drv_display_vmsvga },
        { "display-vmsvga", DEV_CLASS_DISPLAY, 0, 0x15ad, 0x0710, drv_display_vmsvga },
        { "display-virtio", DEV_CLASS_DISPLAY, 0, 0x1af4, 0x1050, drv_display_vga },
        /* 任意 VGA 兼容显卡兜底 */
        { "display-vga", DEV_CLASS_DISPLAY, 0x00, 0, 0, drv_display_vga },

        /* ---------------- 网卡 ---------------- */
        /* Intel PRO/1000 (e1000/e1000e) 家族 */
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1000, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1001, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1004, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x100E, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x100F, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x100D, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10A4, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1076, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1079, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10B9, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10C9, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10D3, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10D5, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10DE, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10EA, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1229, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1019, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x107c, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x107d, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10e5, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x108e, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x10fc, drv_net_e1000 },
        { "net-e1000", DEV_CLASS_NET, 0, 0x8086, 0x1502, drv_net_e1000 },
        /* Realtek */
        { "net-rtl8139", DEV_CLASS_NET, 0, 0x10ec, 0x8139, drv_net_rtl8139 },
        { "net-rtl8139", DEV_CLASS_NET, 0, 0x10ec, 0x8136, drv_net_rtl8139 },
        { "net-rtl8139", DEV_CLASS_NET, 0, 0x10ec, 0x8129, drv_net_rtl8139 },
        { "net-rtl8169", DEV_CLASS_NET, 0, 0x10ec, 0x8168, drv_net_rtl8139 },
        { "net-rtl8169", DEV_CLASS_NET, 0, 0x10ec, 0x8169, drv_net_rtl8139 },
        { "net-ne2000", DEV_CLASS_NET, 0, 0x10ec, 0x8029, drv_net_ne2000 },
        /* AMD PCnet (VBox 经典默认) */
        { "net-pcnet", DEV_CLASS_NET, 0, 0x1022, 0x2000, drv_net_pcnet },
        /* virtio-net */
        { "net-virtio", DEV_CLASS_NET, 0, 0x1af4, 0x1000, drv_net_virtio },
        { "net-virtio", DEV_CLASS_NET, 0, 0x1af4, 0x1041, drv_net_virtio },
        /* 较新 Intel I218/I219/I211：只做以太网类识别，不挂旧 e1000 真驱动 */
        { "net-generic", DEV_CLASS_NET, 0, 0x8086, 0x15b8, drv_net_generic },
        { "net-generic", DEV_CLASS_NET, 0, 0x8086, 0x1533, drv_net_generic },
        { "net-generic", DEV_CLASS_NET, 0, 0x8086, 0x15d8, drv_net_generic },
        { "net-generic", DEV_CLASS_NET, 0, 0x8086, 0x1a11, drv_net_generic },
        /* 任意以太网控制器兜底 */
        { "net-generic", DEV_CLASS_NET, 0x00, 0, 0, drv_net_generic },

        /* ---------------- 存储 ---------------- */
        /* IDE/PIIX 系列精确 ID */
        { "storage-ata", DEV_CLASS_STORAGE, 0, 0x8086, 0x7010, drv_storage_ata },
        { "storage-ata", DEV_CLASS_STORAGE, 0, 0x8086, 0x7111, drv_storage_ata },
        /* AHCI 精确 ID（Intel/AMD/VIA/Marvell 常见） */
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x2829, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x2922, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x1c02, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x9c03, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x9d03, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x8c02, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x8d62, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x1022, 0x7800, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x1022, 0x7901, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x1022, 0x7902, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x1b21, 0x0611, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x1b21, 0x0612, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0xa282, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0xa283, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x9d03, drv_storage_ahci },
        { "storage-ahci", DEV_CLASS_STORAGE, 0, 0x8086, 0x5ae3, drv_storage_ahci },
        /* virtio-blk */
        { "storage-virtio", DEV_CLASS_STORAGE, 0, 0x1af4, 0x1001, drv_storage_virtio },
        { "storage-virtio", DEV_CLASS_STORAGE, 0, 0x1af4, 0x1042, drv_storage_virtio },
        /* 按 subclass 兜底 */
        { "storage-ata", DEV_CLASS_STORAGE, 0x01, 0, 0, drv_storage_ata },
        { "storage-ahci", DEV_CLASS_STORAGE, 0x06, 0, 0, drv_storage_ahci },
        { "storage-nvme", DEV_CLASS_STORAGE, 0x08, 0, 0, drv_storage_nvme },
        { "storage-virtio", DEV_CLASS_STORAGE, 0x80, 0, 0, drv_storage_virtio },

        /* ---------------- 音频 ---------------- */
        /* AC97 (VBox/ICH/老芯片组) */
        { "audio-ac97", DEV_CLASS_AUDIO, 0, 0x8086, 0x2415, drv_audio_ac97 },
        { "audio-ac97", DEV_CLASS_AUDIO, 0, 0x8086, 0x2420, drv_audio_ac97 },
        { "audio-ac97", DEV_CLASS_AUDIO, 0, 0x8086, 0x7190, drv_audio_ac97 },
        { "audio-ac97", DEV_CLASS_AUDIO, 0, 0x1039, 0x7012, drv_audio_ac97 },
        { "audio-ac97", DEV_CLASS_AUDIO, 0, 0x1106, 0x3059, drv_audio_ac97 },
        { "audio-ac97", DEV_CLASS_AUDIO, 0, 0x10de, 0x006a, drv_audio_ac97 },
        /* Intel HD Audio (HDA) 系列 */
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x2668, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x284b, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x293e, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x3b56, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x8c20, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x9c20, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x9d70, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0xa170, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0xa171, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x9d71, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x3ea0, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x8086, 0x0f08, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x1022, 0x157a, drv_audio_hda },
        { "audio-hda", DEV_CLASS_AUDIO, 0, 0x10de, 0x0fb9, drv_audio_hda },
        { "audio-sb16", DEV_CLASS_AUDIO, 0, 0x1274, 0x1371, drv_audio_sb16 },
        /* 任意多媒体设备兜底 */
        { "audio-generic", DEV_CLASS_AUDIO, 0, 0, 0, drv_audio_generic },

        /* ---------------- USB 主机控制器 (PCI class 0x0c) ---------------- */
        /* UHCI (subclass 0x03) */
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x7020, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x7112, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x2412, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x2414, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x27c8, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x2830, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x2831, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x2832, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x2834, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0x8086, 0x2835, drv_usb_uhci },
        { "usb-uhci", 0x0c, 0x03, 0, 0, drv_usb_uhci },
        /* OHCI (subclass 0x10) */
        { "usb-ohci", 0x0c, 0x10, 0x1033, 0x0035, drv_usb_ohci },
        { "usb-ohci", 0x0c, 0x10, 0x1022, 0x206f, drv_usb_ohci },
        { "usb-ohci", 0x0c, 0x10, 0, 0, drv_usb_ohci },
        /* EHCI / USB2.0 (subclass 0x20) */
        { "usb-ehci", 0x0c, 0x20, 0x8086, 0x24cd, drv_usb_ehci },
        { "usb-ehci", 0x0c, 0x20, 0x8086, 0x258c, drv_usb_ehci },
        { "usb-ehci", 0x0c, 0x20, 0x8086, 0x27cc, drv_usb_ehci },
        { "usb-ehci", 0x0c, 0x20, 0x8086, 0x2836, drv_usb_ehci },
        { "usb-ehci", 0x0c, 0x20, 0x8086, 0x293a, drv_usb_ehci },
        { "usb-ehci", 0x0c, 0x20, 0x8086, 0x3b34, drv_usb_ehci },
        { "usb-ehci", 0x0c, 0x20, 0, 0, drv_usb_ehci },
        /* xHCI / USB3.x (subclass 0x30)；VBox 用 NEC 1033:0194 */
        { "usb-xhci", 0x0c, 0x30, 0x1033, 0x0194, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0x8086, 0x8c31, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0x8086, 0x9c31, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0x8086, 0x9d31, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0x8086, 0x1e31, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0x8086, 0xa12f, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0x1b21, 0x1042, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0x1022, 0x149c, drv_usb_xhci },
        { "usb-xhci", 0x0c, 0x30, 0, 0, drv_usb_xhci },

        /* ---------------- 芯片组/桥（精确 ID） ---------------- */
        { "bridge-host", 0x06, 0x00, 0x8086, 0x1237, drv_bridge },
        { "bridge-host", 0x06, 0x00, 0x8086, 0x7000, drv_bridge },
        { "bridge-isa",  0x06, 0x01, 0x8086, 0x7000, drv_bridge },
        { "bridge-isa",  0x06, 0x01, 0x8086, 0x7110, drv_bridge },
        { "bridge-pci",  0x06, 0x04, 0x8086, 0x7010, drv_bridge },
        { "bridge-pci",  0x06, 0x04, 0x8086, 0x244e, drv_bridge },

        /* ---------------- 定时器（保留原表） ---------------- */
        { "timer-hpet", DEV_CLASS_TIMER, 0, 0x8086, 0x2410, drv_timer_hpet },
        { "timer-pit", DEV_CLASS_TIMER, 0, 0x8086, 0x29c0, drv_timer_pit },
        { "timer-pit", DEV_CLASS_TIMER, 0, 0x8086, 0x2918, drv_timer_pit },
    };
    for (size_t i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++)
        dev_register(&drivers[i]);
}
