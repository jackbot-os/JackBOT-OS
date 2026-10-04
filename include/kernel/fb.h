#ifndef JACKBOT_FB_H
#define JACKBOT_FB_H

#include <stdint.h>

int fb_bochs_init(uint32_t width, uint32_t height);
int fb_present(void);
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch(void);
uint8_t fb_bpp(void);

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);
void fb_clear(uint32_t packed);
void fb_putpixel(int x, int y, uint32_t packed);
void fb_fill_rect(int x, int y, int w, int h, uint32_t packed);
void fb_blit(int dx, int dy, int w, int h, const uint32_t *pixels);

#endif
