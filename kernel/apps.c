#include <stdint.h>
#include <kernel/apps.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

void power_open(void);    int power_frame(int, int, int);
void store_open(void);    int store_frame(int, int, int);
void firewall_open(void); int firewall_frame(int, int, int);
void backup_open(void);   int backup_frame(int, int, int);
void capture_open(void);  int capture_frame(int, int, int);
void calendar_open(void); int calendar_frame(int, int, int);
void fontmgr_open(void);  int fontmgr_frame(int, int, int);
void region_open(void);   int region_frame(int, int, int);
void network_open(void);  int network_frame(int, int, int);
void audio_open(void);    int audio_frame(int, int, int);
void update_open(void);  int update_frame(int, int, int);
void browser_open(void); int browser_frame(int, int, int);
void gpu3d_open(void);    int gpu3d_frame(int, int, int);
void diskmgr_open(void);  int diskmgr_frame(int, int, int);
void about_open(void);    int about_frame(int, int, int);
void calc_open(void);     int calc_frame(int, int, int);
void notepad_open(void);  int notepad_frame(int, int, int);
void monitor_open(void);  int monitor_frame(int, int, int);
void python_open(void);   int python_frame(int, int, int);
void jca_open(void);      int jca_frame(int, int, int);
void usbdev_open(void);   int usbdev_frame(int, int, int);
void battery_open(void);  int battery_frame(int, int, int);
void recycleapp_open(void); int recycleapp_frame(int, int, int);
void files_open(void);    int files_frame(int, int, int);
void devices_open(void);  int devices_frame(int, int, int);
void voice_open(void);    int voice_frame(int, int, int);
void bluetooth_open(void); int bluetooth_frame(int, int, int);
void snake_open(void);    int snake_frame(int, int, int);
void g2048_open(void);    int g2048_frame(int, int, int);

typedef void (*openfn)(void);
typedef int (*framefn)(int, int, int);

static openfn opens[APP_LIMIT];
static framefn frames[APP_LIMIT];
static int done;

static void set(int id, openfn o, framefn f)
{
    opens[id] = o;
    frames[id] = f;
}

static void setup(void)
{
    if (done)
        return;
    set(APP_POWER, power_open, power_frame);
    set(APP_STORE, store_open, store_frame);
    set(APP_FIREWALL, firewall_open, firewall_frame);
    set(APP_BACKUP, backup_open, backup_frame);
    set(APP_CAPTURE, capture_open, capture_frame);
    set(APP_CALENDAR, calendar_open, calendar_frame);
    set(APP_FONTMGR, fontmgr_open, fontmgr_frame);
    set(APP_REGION, region_open, region_frame);
    set(APP_NETWORK, network_open, network_frame);
    set(APP_AUDIO, audio_open, audio_frame);
    set(APP_UPDATE, update_open, update_frame);
    set(APP_BROWSER, browser_open, browser_frame);
    set(APP_3DSTUDIO, gpu3d_open, gpu3d_frame);
    set(APP_DISKMGR, diskmgr_open, diskmgr_frame);
    set(APP_ABOUT, about_open, about_frame);
    set(APP_CALC, calc_open, calc_frame);
    set(APP_NOTEPAD, notepad_open, notepad_frame);
    set(APP_MONITOR, monitor_open, monitor_frame);
    set(APP_PYTHON, python_open, python_frame);
    set(APP_JCA, jca_open, jca_frame);
    set(APP_USB, usbdev_open, usbdev_frame);
    set(APP_BATTERY, battery_open, battery_frame);
    set(APP_RECYCLE, recycleapp_open, recycleapp_frame);
    set(APP_FILES, files_open, files_frame);
    set(APP_DEVICES, devices_open, devices_frame);
    set(APP_VOICE, voice_open, voice_frame);
    set(APP_BLUETOOTH, bluetooth_open, bluetooth_frame);
    set(APP_SNAKE, snake_open, snake_frame);
    set(APP_G2048, g2048_open, g2048_frame);
    done = 1;
}

void app_open(int id)
{
    setup();
    if (id >= APP_POWER && id < APP_LIMIT && opens[id])
        opens[id]();
}

int app_frame(int id, int mx, int my, int clicked)
{
    setup();
    if (id >= APP_POWER && id < APP_LIMIT && frames[id])
        return frames[id](mx, my, clicked);
    return 1;
}

const char *app_name(int id)
{
    setup();
    switch (id) {
    case APP_POWER: return tr(STR_AN_POWER);
    case APP_STORE: return tr(STR_AN_STORE);
    case APP_FIREWALL: return tr(STR_AN_FIREWALL);
    case APP_BACKUP: return tr(STR_AN_BACKUP);
    case APP_CAPTURE: return tr(STR_AN_CAPTURE);
    case APP_CALENDAR: return tr(STR_AN_CALENDAR);
    case APP_FONTMGR: return tr(STR_AN_FONTMGR);
    case APP_REGION: return tr(STR_AN_REGION);
    case APP_NETWORK: return tr(STR_AN_NETWORK);
    case APP_AUDIO: return tr(STR_AN_AUDIO);
    case APP_UPDATE: return tr(STR_AN_UPDATE);
    case APP_BROWSER: return tr(STR_AN_BROWSER);
    case APP_3DSTUDIO: return tr(STR_AN_3DSTUDIO);
    case APP_DISKMGR: return tr(STR_AN_DISKMGR);
    case APP_ABOUT: return tr(STR_AN_ABOUT);
    case APP_CALC: return tr(STR_AN_CALC);
    case APP_NOTEPAD: return tr(STR_AN_NOTEPAD);
    case APP_MONITOR: return tr(STR_AN_MONITOR);
    case APP_PYTHON: return tr(STR_AN_PYTHON);
    case APP_JCA: return tr(STR_AN_JCA);
    case APP_USB: return tr(STR_AN_USB);
    case APP_BATTERY: return tr(STR_AN_BATTERY);
    case APP_RECYCLE: return tr(STR_AN_RECYCLE);
    case APP_FILES: return tr(STR_AN_FILES);
    case APP_DEVICES: return tr(STR_AN_DEVICES);
    case APP_VOICE: return tr(STR_AN_VOICE);
    case APP_BLUETOOTH: return tr(STR_AN_BLUETOOTH);
    case APP_SNAKE: return tr(STR_AN_SNAKE);
    case APP_G2048: return tr(STR_AN_G2048);
    default: return "";
    }
}

int app_persistent(int id)
{
    return id == APP_DISKMGR || id == APP_ABOUT || id == APP_NOTEPAD
        || id == APP_MONITOR || id == APP_PYTHON || id == APP_JCA
        || id == APP_USB || id == APP_BATTERY || id == APP_RECYCLE
        || id == APP_FILES || id == APP_DEVICES;
}

int app_refresh_period(int id)
{
    return id == APP_MONITOR ? 30 : 0;
}
