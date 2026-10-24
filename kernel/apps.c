#include <stdint.h>
#include <kernel/apps.h>

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

typedef void (*openfn)(void);
typedef int (*framefn)(int, int, int);

static openfn opens[APP_LIMIT];
static framefn frames[APP_LIMIT];
static const char *names[APP_LIMIT];
static int done;

static void set(int id, const char *n, openfn o, framefn f)
{
    names[id] = n;
    opens[id] = o;
    frames[id] = f;
}

static void setup(void)
{
    if (done)
        return;
    set(APP_POWER, "Power", power_open, power_frame);
    set(APP_STORE, "App Store", store_open, store_frame);
    set(APP_FIREWALL, "Firewall", firewall_open, firewall_frame);
    set(APP_BACKUP, "Backup", backup_open, backup_frame);
    set(APP_CAPTURE, "Screenshot", capture_open, capture_frame);
    set(APP_CALENDAR, "Calendar", calendar_open, calendar_frame);
    set(APP_FONTMGR, "Font Manager", fontmgr_open, fontmgr_frame);
    set(APP_REGION, "Time Zone", region_open, region_frame);
    set(APP_NETWORK, "Network", network_open, network_frame);
    set(APP_AUDIO, "Audio", audio_open, audio_frame);
    set(APP_UPDATE, "System Update", update_open, update_frame);
    set(APP_BROWSER, "JBrowser", browser_open, browser_frame);
    set(APP_3DSTUDIO, "3D Studio", gpu3d_open, gpu3d_frame);
    set(APP_DISKMGR, "This PC", diskmgr_open, diskmgr_frame);
    set(APP_ABOUT, "About", about_open, about_frame);
    set(APP_CALC, "Calculator", calc_open, calc_frame);
    set(APP_NOTEPAD, "Notepad", notepad_open, notepad_frame);
    set(APP_MONITOR, "System Monitor", monitor_open, monitor_frame);
    set(APP_PYTHON, "Python", python_open, python_frame);
    set(APP_JCA, "JCA Studio", jca_open, jca_frame);
    set(APP_USB, "USB Devices", usbdev_open, usbdev_frame);
    set(APP_BATTERY, "Battery & Power", battery_open, battery_frame);
    set(APP_RECYCLE, "Recycle Bin", recycleapp_open, recycleapp_frame);
    set(APP_FILES, "File Explorer", files_open, files_frame);
    set(APP_DEVICES, "Device Manager", devices_open, devices_frame);
    set(APP_VOICE, "Jbot Voice", voice_open, voice_frame);
    set(APP_BLUETOOTH, "Bluetooth", bluetooth_open, bluetooth_frame);
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
    if (id >= APP_POWER && id < APP_LIMIT)
        return names[id];
    return "";
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
