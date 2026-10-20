#ifndef JACKBOT_FB_H
#define JACKBOT_FB_H

#include <stdint.h>

int fb_bochs_init(uint32_t width, uint32_t height);
int fb_linear_init(uint64_t phys, uint32_t width, uint32_t height,
                   uint32_t pitch, uint8_t bpp,
                   uint8_t rpos, uint8_t rsize,
                   uint8_t gpos, uint8_t gsize,
                   uint8_t bpos, uint8_t bsize);
int fb_present(void);
int fb_rebind_gpu(uint64_t phys, uint32_t width, uint32_t height,
                  uint32_t pitch);
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch(void);
uint8_t fb_bpp(void);

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);
void fb_clear(uint32_t packed);
void fb_putpixel(int x, int y, uint32_t packed);
uint32_t fb_getpixel(int x, int y);
uint32_t fb_mix(uint32_t fg, uint32_t bg, uint8_t cover);
void fb_putpixel_blend(int x, int y, uint32_t fg, uint8_t cover);
void fb_fill_rect(int x, int y, int w, int h, uint32_t packed);
void fb_blit(int dx, int dy, int w, int h, const uint32_t *pixels);
void fb_blit_full(const uint32_t *pixels);

void fb_mark_dirty(int x, int y, int w, int h);
void fb_invalidate(void);
void fb_present_dirty(void);
void fb_present_all(void);

#endif
