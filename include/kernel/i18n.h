#ifndef KERNEL_I18N_H
#define KERNEL_I18N_H

void i18n_init(void);
int i18n_count(void);
int i18n_set_lang(int idx);
int i18n_set_code(const char *code);
int i18n_lang(void);
const char *i18n_code(int idx);
const char *i18n_name(int idx);
const char *tr(int id);

#endif
