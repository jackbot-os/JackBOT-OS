#ifndef JACKBOT_WLAN_H
#define JACKBOT_WLAN_H
/*
 * USB / PCI 无线网卡识别层。
 * 这一层做的是驱动级探测：读 USB 描述符（设备/配置/接口/端点/字符串），
 * 按厂商分类把设备交给对应芯片族的驱动，芯片族驱动再做厂商寄存器读。
 * 它不包含 802.11 关联与加密，所以 state 只会是 identified / register-ok。
 */
#include <stdint.h>

#define WLAN_MAX_DEV  8
#define WLAN_STR_MAX  48
#define WLAN_EP_MAX   4

struct wlan_dev {
    uint16_t vid, pid;
    uint8_t slot, port, speed;
    uint8_t if_class, if_subclass, if_protocol;
    uint8_t n_eps;
    uint8_t ep_in, ep_out, ep_irq;      /* endpoint numbers, 0 = none */
    uint8_t ep_in_maxpkt, ep_out_maxpkt;
    uint8_t mac[6];
    int mac_valid;
    int fw_required;
    const char *family;
    const char *driver;                 /* claiming family driver, "none" */
    char product[WLAN_STR_MAX];
    char serial[WLAN_STR_MAX];
    char state[WLAN_STR_MAX];
    uint32_t reg0;                      /* raw value of chip register 0 */
    int reg_ok;
};

struct wlan_driver {
    const char *name;
    const char *family;
    int fw_required;
    int (*match)(uint16_t vid, uint16_t pid);
    int (*probe)(struct wlan_dev *d);
};

/* Scan the USB and PCI buses, fill the device table, return device count. */
int wlan_scan(void);
int wlan_count(void);
const struct wlan_dev *wlan_dev_get(int idx);
int wlan_register(const struct wlan_driver *d);

/* Transport helpers used by the per-family drivers. */
int wlan_ctrl_in(uint8_t slot, uint8_t req, uint16_t val, uint16_t idx,
                 void *buf, uint16_t len);
int wlan_ctrl_out(uint8_t slot, uint8_t req, uint16_t val, uint16_t idx,
                  const void *buf, uint16_t len);
int wlan_read_desc(uint8_t slot, uint8_t type, uint8_t index, void *buf,
                   uint16_t len);
int wlan_cfg_parse(struct wlan_dev *d);

/* Family drivers. */
extern const struct wlan_driver wlan_rtl8xxxu;     /* Realtek USB 11n/11ac */
extern const struct wlan_driver wlan_rt2800;       /* Ralink RT2x00 USB */
extern const struct wlan_driver wlan_ath9k_htc;    /* Atheros AR9271 USB */
extern const struct wlan_driver wlan_mt7601u;      /* MediaTek MT7601U USB */

#endif
