#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/bluetooth.h>
#include <kernel/pci.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

/*
 * Stage 1 Bluetooth layer: adapter discovery over PCI, HCI state machine and
 * the smart-LED control surface. See bluetooth.h for scope.
 */

static struct bt_adapter adap[BT_MAX_ADAPTERS];
static int n_adap;
static int powered;
static int scan_progress;
static int led_color = BT_LED_OFF;
static int led_bright = 60;
static int led_mode = BT_LED_STEADY;

/* RGB values shown in the UI and handed to the future lamp backend. */
static const uint32_t led_rgb[BT_LED_COLORS] = {
    0x000000, 0xffffff, 0xff3333, 0x33dd55,
    0x3388ff, 0xffcc33, 0x33dddd, 0xdd44dd,
};

static const char *const led_names[BT_LED_COLORS] = {
    "Off", "White", "Red", "Green", "Blue", "Yellow", "Cyan", "Magenta",
};

static const char *vendor_name(uint16_t v)
{
    switch (v) {
    case 0x8087: return "Intel BT";
    case 0x0a5c: return "Broadcom BT";
    case 0x0489: return "Foxconn BT";
    case 0x0bda: return "Realtek BT";
    case 0x13d3: return "Azurewave BT";
    case 0x04ca: return "LiteOn BT";
    default:     return "Bluetooth";
    }
}

static void name_for(struct bt_adapter *a)
{
    const char *vn = vendor_name(a->vendor);
    int i = 0;
    while (vn[i] && i < BT_NAME_LEN - 1) {
        a->name[i] = vn[i];
        i++;
    }
    a->name[i] = 0;
}

int bt_init(void)
{
    memset(adap, 0, sizeof(adap));
    n_adap = 0;
    scan_progress = 0;

    /* PCI scan: class 0x0D wireless, subclass 0x11 Bluetooth. */
    int n = pci_count();
    for (int i = 0; i < n && n_adap < BT_MAX_ADAPTERS; i++) {
        struct pci_dev *d = pci_get(i);
        if (!d)
            continue;
        if (d->class_code == BT_PCI_CLASS &&
            d->subclass == BT_PCI_SUBCLASS) {
            struct bt_adapter *a = &adap[n_adap++];
            a->present = 1;
            a->bus = d->bus;
            a->dev = d->dev;
            a->func = d->func;
            a->vendor = d->vendor;
            a->device = d->device;
            a->state = BT_STATE_DOWN;
            a->powered = 0;
            name_for(a);
            serial_puts("[BT] PCI adapter ");
            serial_puthex(a->vendor);
            serial_puthex(a->device);
            serial_putc('\n');
        }
    }

    serial_puts("[BT] adapters=");
    serial_puthex((uint32_t)n_adap);
    serial_puts(" power=");
    serial_puthex((uint32_t)powered);
    serial_putc('\n');
    return n_adap;
}

int bt_present(void) { return n_adap > 0; }
int bt_count(void) { return n_adap; }
const struct bt_adapter *bt_get(int idx)
{
    if (idx < 0 || idx >= n_adap)
        return 0;
    return &adap[idx];
}

int bt_power(void) { return powered; }

/*
 * HCI bring-up sequence. On a real transport the command bytes are written
 * to the USB/bulk or UART endpoint; stage 1 performs the state transitions
 * and logs the exact commands so the backend slot is well defined.
 */
static void hci_reset_sequence(struct bt_adapter *a)
{
    /* HCI_Reset (0x0c03, no params), then Read_Local_Version (0x1001). */
    serial_puts("[BT] HCI Reset -> ");
    a->state = BT_STATE_RESET;
    a->state = BT_STATE_UP;
    serial_puts("Read_Local_Version -> UP\n");
}

int bt_set_power(int on)
{
    if (on && n_adap == 0) {
        /* No PCI adapter: keep the software state (USB-BT dongle support
           arrives with USB device enumeration), but say so on the log. */
        serial_puts("[BT] no PCI adapter; power state kept software-only\n");
        powered = 1;
        return 0;
    }
    powered = on ? 1 : 0;
    for (int i = 0; i < n_adap; i++) {
        if (on) {
            adap[i].powered = 1;
            hci_reset_sequence(&adap[i]);
        } else {
            adap[i].powered = 0;
            adap[i].state = BT_STATE_DOWN;
            serial_puts("[BT] adapter powered down\n");
        }
    }
    return 1;
}

int bt_begin_scan(void)
{
    if (!powered)
        return -1;
    scan_progress = 1;
    /* HCI Inquiry (0x0401) is issued by the transport backend. */
    serial_puts("[BT] inquiry requested (transport backend pending)\n");
    return 0;
}

int bt_scan_progress(void)
{
    if (scan_progress && scan_progress < 100)
        scan_progress += 10;
    return scan_progress;
}

void bt_led_set(int color)
{
    if (color < 0 || color >= BT_LED_COLORS)
        return;
    led_color = color;
    /* Future backend: SET_REPORT / GATT write with led_rgb[color] and the
       current brightness. Logged so the path is observable end to end. */
    serial_puts("[BT] LED color=");
    serial_puthex((uint32_t)color);
    serial_puts(" rgb=");
    serial_puthex(led_rgb[color]);
    serial_putc('\n');
}

void bt_led_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    led_bright = pct;
}

void bt_led_mode(int mode)
{
    if (mode < 0 || mode > BT_LED_BREATHE)
        return;
    led_mode = mode;
}

int bt_led_color(void) { return led_color; }
int bt_led_brightness_get(void) { return led_bright; }
int bt_led_mode_get(void) { return led_mode; }
uint32_t bt_led_rgb(int color)
{
    if (color < 0 || color >= BT_LED_COLORS)
        return 0;
    return led_rgb[color];
}
const char *bt_led_color_name(int color)
{
    if (color < 0 || color >= BT_LED_COLORS)
        return "?";
    return led_names[color];
}

void bt_self_test(void)
{
    int ok = 1;
    int saved = led_color;
    int saved_pow = powered;

    bt_led_set(BT_LED_RED);
    if (led_color != BT_LED_RED) ok = 0;
    bt_led_set(BT_LED_BLUE);
    if (led_color != BT_LED_BLUE) ok = 0;
    bt_led_set(BT_LED_OFF);
    bt_led_brightness(45);
    if (led_bright != 45) ok = 0;
    bt_led_brightness(-5);
    if (led_bright != 0) ok = 0;
    bt_led_brightness(200);
    if (led_bright != 100) ok = 0;
    bt_led_brightness(60);
    bt_led_mode(BT_LED_BLINK);
    if (led_mode != BT_LED_BLINK) ok = 0;
    bt_led_mode(BT_LED_STEADY);
    if (bt_led_rgb(BT_LED_GREEN) != 0x33dd55) ok = 0;
    bt_led_set(saved);
    powered = saved_pow;

    serial_puts(ok ? "[BT] selftest PASS\n" : "[BT] selftest FAIL\n");
}
