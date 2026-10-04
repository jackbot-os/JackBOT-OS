#ifndef PS2_H
#define PS2_H
#include <stdint.h>

void ps2_init(void);
void ps2_kbd_irq(void);
void ps2_mouse_irq(void);
int ps2_read_key(char *out);
void ps2_mouse_xy(int *x, int *y);
int ps2_mouse_click(int *x, int *y);
int ps2_mouse_down(void);

#endif
