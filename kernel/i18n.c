#include <stdint.h>
#include <stddef.h>
#include <kernel/i18n.h>
#include "i18n_data.h"

static int lang;

void i18n_init(void) { lang = 0; }

int i18n_count(void) { return I18N_NLANG; }

int i18n_set_lang(int idx)
{
    if (idx < 0 || idx >= I18N_NLANG)
        return lang;
    lang = idx;
    return lang;
}

int i18n_set_code(const char *code)
{
    for (int i = 0; i < I18N_NLANG; i++) {
        const char *a = i18n_codes[i], *b = code;
        int same = 1;
        while (*a || *b) {
            if (*a != *b)
                same = 0;
            a++;
            b++;
        }
        if (same)
            return i18n_set_lang(i);
    }
    return lang;
}

int i18n_lang(void) { return lang; }
int i18n_rtl_idx(int idx)
{
    return (idx >= 0 && idx < I18N_NLANG) ? (int)i18n_rtl_flags[idx] : 0;
}
int i18n_rtl(void) { return i18n_rtl_idx(lang); }
const char *i18n_code(int idx)
{
    return (idx >= 0 && idx < I18N_NLANG) ? i18n_codes[idx] : "";
}
const char *i18n_name(int idx)
{
    return (idx >= 0 && idx < I18N_NLANG) ? i18n_names[idx] : "";
}
const char *tr(int id)
{
    if (id < 0 || id >= I18N_NKEY)
        return "";
    return i18n_str[lang][id];
}
