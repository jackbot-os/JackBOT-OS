#ifndef APPS_H
#define APPS_H

enum {
    APP_POWER = 4,
    APP_STORE,
    APP_FIREWALL,
    APP_BACKUP,
    APP_CAPTURE,
    APP_CALENDAR,
    APP_FONTMGR,
    APP_REGION,
    APP_NETWORK,
    APP_AUDIO,
    APP_UPDATE,
    APP_BROWSER,
    APP_3DSTUDIO,
    APP_DISKMGR,
    APP_ABOUT,
    APP_CALC,
    APP_NOTEPAD,
    APP_MONITOR,
    APP_PYTHON,
    APP_JCA,
    APP_USB,
    APP_BATTERY,
    APP_RECYCLE,
    APP_FILES,
    APP_DEVICES,
    APP_VOICE,
    APP_BLUETOOTH,
    APP_SNAKE,
    APP_G2048,
    APP_LIMIT
};

void app_open(int id);
int app_frame(int id, int mx, int my, int clicked);
const char *app_name(int id);
int app_persistent(int id);
int app_refresh_period(int id);

#endif
