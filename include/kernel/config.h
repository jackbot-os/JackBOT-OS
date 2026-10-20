#ifndef JACKBOT_CONFIG_H
#define JACKBOT_CONFIG_H
#include <stdint.h>

void config_init(void);
int config_language(void);
int config_font(void);
int config_tz(void);
int config_fw(void);
int config_oobe_done(void);
int config_keyboard(void);
void config_set_language(int l);
void config_set_font(int f);
void config_set_tz(int v);
void config_set_fw(int v);
void config_set_oobe_done(int v);
void config_set_keyboard(int v);
void config_save(void);
void config_self_test(void);

#endif
