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
int config_honeypot(void);
int config_region(void);
int config_volume(void);
int config_mic_perm(void);
int config_voice_perm(void);
int config_cam_perm(void);
int config_loc_perm(void);
int config_net_perm(void);
int config_glass(void);
void config_set_language(int l);
void config_set_font(int f);
void config_set_tz(int v);
void config_set_fw(int v);
void config_set_oobe_done(int v);
void config_set_keyboard(int v);
void config_set_honeypot(int v);
void config_set_region(int v);
void config_set_volume(int v);
void config_set_mic_perm(int v);
void config_set_voice_perm(int v);
void config_set_cam_perm(int v);
void config_set_loc_perm(int v);
void config_set_net_perm(int v);
void config_set_glass(int v);
void config_save(void);
void config_self_test(void);

#endif
