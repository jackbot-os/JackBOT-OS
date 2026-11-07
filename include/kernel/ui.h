#ifndef UI_H
#define UI_H
#include <stdint.h>

void ui_set_scale(int s);
/* Liquid-glass switch: translucent window chrome over the desktop. */
void ui_set_glass(int on);
int ui_glass(void);
int ui_scale(void);

void ui_rect(int x, int y, int w, int h, uint32_t c);
void ui_border(int x, int y, int w, int h, uint32_t c);
void ui_round_rect(int x, int y, int w, int h, int r, uint32_t c);
void ui_round_border(int x, int y, int w, int h, int r, uint32_t c);
int ui_inside(int px, int py, int x, int y, int w, int h);

int ui_button(int x, int y, int w, int h, const char *label, int sel,
              int mx, int my, int clicked);

struct ui_win {
    int x, y, w, h;
};

/* Scale a design-sized window (dimensions at scale 1) to the current UI
   scale, centre it in the work area above the task bar, and clamp it so it
   never runs off the screen. Every app opens its window through this. */
void ui_fit(int design_w, int design_h, struct ui_win *win);

int ui_window(struct ui_win *win, const char *title,
              int mx, int my, int clicked);
int ui_close_hit(int mx, int my);

#endif
