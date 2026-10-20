#ifndef JACKBOT_NIC_H
#define JACKBOT_NIC_H
/*
 * NIC 抽象层（网卡兼容层核心）
 * 网络栈通过 nic_ops 函数指针收发帧，不关心具体芯片；
 * e1000 / rtl8139 / ne2000 等驱动各自实现并注册。
 */
#include <stdint.h>
#include <stddef.h>
#include <kernel/pci.h>

#define NIC_MAX 4
#define NIC_NAME_MAX 16

struct nic_ops {
    const char *name;
    int (*init)(struct pci_dev *d);          /* 初始化，0=成功 */
    int (*send)(const void *data, uint16_t len);
    int (*recv)(void *buf, uint16_t cap);
    void (*get_mac)(uint8_t out[6]);
    int (*present)(void);
};

int nic_register(const struct nic_ops *ops);
/* 探测全部已注册驱动，选第一个成功初始化的作为活动网卡 */
int nic_init_all(void);
int nic_active(void);                        /* 是否已有活动网卡 */
int nic_send(const void *data, uint16_t len);
int nic_recv(void *buf, uint16_t cap);
void nic_get_mac(uint8_t out[6]);
const char *nic_active_name(void);

#endif
