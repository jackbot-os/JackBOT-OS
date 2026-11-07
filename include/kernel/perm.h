#ifndef JACKBOT_PERM_H
#define JACKBOT_PERM_H
#include <stdint.h>

enum {
    PERM_MIC = 0,
    PERM_VOICE,
    PERM_CAMERA,
    PERM_LOCATION,
    PERM_NETWORK,
    PERM_COUNT,
};

int perm_granted(int which);
void perm_allow(int which);
void perm_deny(int which);

/*
 * Modal permission prompt drawn inside the current window. Returns 1 while
 * the prompt is still open; 0 once the user chose Allow or Deny. The result
 * is persisted via config on the click.
 */
int perm_prompt(int which, int win_x, int win_y, int win_w, int win_h,
                int mx, int my, int clicked);

#endif
