#ifndef PS2_H
#define PS2_H
#include <stdint.h>

#define JB_KEY_UP    0x11
#define JB_KEY_DOWN  0x12
#define JB_KEY_LEFT  0x13
#define JB_KEY_RIGHT 0x14
#define JB_KEY_WIN   0x15
#define JB_KEY_F1    0x16
#define JB_KEY_F2    0x17
#define JB_KEY_F3    0x18

void ps2_init(void);
void ps2_kbd_irq(void);
void ps2_mouse_irq(void);
int ps2_read_key(char *out);
int ps2_key_pending(void);
int ps2_consume_key(char want);
void ps2_mouse_xy(int *x, int *y);
int ps2_mouse_click(int *x, int *y);
int ps2_mouse_right_click(int *x, int *y);
int ps2_mouse_down(void);
int ps2_mouse_wheel(void);   /* wheel delta since the last call (0 = none) */
void ps2_mouse_flush(void);

#endif
