#ifndef JACKBOT_BLUETOOTH_H
#define JACKBOT_BLUETOOTH_H
/*
 * JackBOT OS Bluetooth subsystem (stage 1: adapter discovery + HCI state
 * machine + smart-LED control surface).
 *
 * Stage 1 really does:
 *   - PCI bus scan for wireless controllers of Bluetooth subclass (0x0D11)
 *   - HCI transport state machine: DOWN -> RESET -> UP (command sequencing
 *     point; transport backends for USB-BT/UART are wired in later stages)
 *   - smart LED control surface: color / brightness / blink mode, applied to
 *     a connected HID/BLE lamp once the USB control-transfer backend lands
 *
 * Not implemented yet (reported honestly in the UI):
 *   - full L2CAP/ATT/GATT stack, pairing, SDP
 *   - USB device-level enumeration (the USB layer currently exposes host
 *     controllers and root-hub port status only)
 */
#include <stdint.h>

#define BT_MAX_ADAPTERS 4
#define BT_NAME_LEN     24

/* PCI base class 0x0D wireless, subclass 0x11 Bluetooth */
#define BT_PCI_CLASS    0x0D
#define BT_PCI_SUBCLASS 0x11

enum {
    BT_STATE_DOWN = 0,
    BT_STATE_RESET,
    BT_STATE_UP,
};

/* Smart LED palette (RGB encoded for the lamp backend). */
enum {
    BT_LED_OFF = 0,
    BT_LED_WHITE,
    BT_LED_RED,
    BT_LED_GREEN,
    BT_LED_BLUE,
    BT_LED_YELLOW,
    BT_LED_CYAN,
    BT_LED_MAGENTA,
    BT_LED_COLORS
};

enum {
    BT_LED_STEADY = 0,
    BT_LED_BLINK,
    BT_LED_BREATHE,
};

struct bt_adapter {
    uint8_t present;
    uint8_t bus, dev, func;
    uint16_t vendor;
    uint16_t device;
    uint8_t state;
    uint8_t powered;
    char name[BT_NAME_LEN];
};

/* Re-scan the PCI bus for Bluetooth controllers. Returns adapter count. */
int bt_init(void);
int bt_present(void);
int bt_count(void);
const struct bt_adapter *bt_get(int idx);

/* Software power switch; when an adapter is present this walks the HCI
   Reset -> Read Local Version bring-up sequence (logged on serial). */
int bt_set_power(int on);
int bt_power(void);

/* Inquiry placeholder: returns number of cached remote devices.
   Stage 1 keeps the state machine and UI path; HCI inquiry comes with the
   transport backend. */
int bt_begin_scan(void);
int bt_scan_progress(void);

/* Smart LED control surface. */
void bt_led_set(int color);
void bt_led_brightness(int pct);
void bt_led_mode(int mode);
int bt_led_color(void);
int bt_led_brightness_get(void);
int bt_led_mode_get(void);
uint32_t bt_led_rgb(int color);
const char *bt_led_color_name(int color);

void bt_self_test(void);

#endif
