#ifndef JACKBOT_DEV_H
#define JACKBOT_DEV_H
/*
 * JackBOT OS 设备驱动框架（主机兼容层核心）
 * 统一设备模型 + 驱动注册表：按 PCI vendor:device 或 class:subclass 匹配，
 * 让同一内核兼容不同品牌主机硬件（显卡/网卡/声卡/磁盘/输入/定时器）。
 */
#include <stdint.h>
#include <stddef.h>
#include <kernel/pci.h>

/* ---- 设备类别（与 PCI class code 对齐） ---- */
#define DEV_CLASS_STORAGE 0x01
#define DEV_CLASS_NET     0x02
#define DEV_CLASS_DISPLAY 0x03
#define DEV_CLASS_AUDIO   0x04
#define DEV_CLASS_INPUT   0x05
#define DEV_CLASS_TIMER   0x06
#define DEV_CLASS_OTHER   0x7F

#define DEV_MAX 80
#define DEV_NAME_MAX 24

typedef int (*dev_init_fn)(struct pci_dev *d);

struct dev_driver {
    const char *name;
    uint8_t class_code;        /* 匹配 PCI class，0 忽略 */
    uint8_t subclass;          /* 匹配 PCI subclass，0 忽略 */
    uint16_t vendor;           /* 匹配 vendor，0 忽略 */
    uint16_t device;           /* 匹配 device，0 忽略 */
    dev_init_fn init;
};

struct dev_instance {
    const struct dev_driver *drv;
    struct pci_dev *pci;
    uint8_t class;
    uint8_t subclass;
    char name[DEV_NAME_MAX];
    int ok;                    /* 驱动初始化成功 */
};

/* ---- 驱动注册 ---- */
void dev_register(const struct dev_driver *drv);
/* ---- 对所有 PCI 设备做驱动匹配并初始化，返回成功数 ---- */
int dev_probe_all(void);
/* ---- 按类别查询已识别设备 ---- */
struct dev_instance *dev_find(uint8_t class_code, int idx);
int dev_count(uint8_t class_code);
int dev_driver_count(void);
int dev_instance_count(void);
void dev_print_all(void);

#endif
