#ifndef DESKTOP_H
#define DESKTOP_H
#include <stdint.h>

void desktop_init(void);
void desktop_run(void);

/* Install a decoded picture (sw x sh, packed 0x00RRGGBB) as wallpaper. */
void wallpaper_set_custom(const uint32_t *src, int sw, int sh);

/* Streaming wallpaper install: the decoder hands over one source row at a
   time and the desktop scales it straight into wp_buf, so a large picture
   never needs a full-size temporary pixel buffer. */
void wallpaper_stream_begin(int sw, int sh);
void wallpaper_stream_row(int sy, const uint32_t *row, int sw);
void wallpaper_stream_end(void);

#endif
