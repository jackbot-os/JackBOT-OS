#ifndef JACKBOT_CONFIG_H
#define JACKBOT_CONFIG_H
#include <stdint.h>

void config_init(void);
int config_language(void);
int config_font(void);
void config_set_language(int l);
void config_set_font(int f);
void config_save(void);
void config_self_test(void);

#endif
