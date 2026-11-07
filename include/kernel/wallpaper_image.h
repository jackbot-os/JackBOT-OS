#ifndef JACKBOT_WALLPAPER_IMAGE_H
#define JACKBOT_WALLPAPER_IMAGE_H
#include <stdint.h>

#define WIMG_NAME_MAX 40
#define WIMG_MAX_FILES 12

struct wimg_file {
    char name[WIMG_NAME_MAX];
    char short83[13];
};

/* List picture files in the root directory. Returns count, <0 on error. */
int wimg_list(struct wimg_file *out, int max);

/* Read and decode a picture by its 8.3 short name and install it.
   Returns 0 on success, <0 on failure (see wimg_status). */
int wimg_apply(const char *short83);

/* Install the built-in default wallpaper (East Lake photo linked into the
   kernel). Decoded as a stream straight into the wallpaper buffer. */
int wimg_apply_default(void);

/* Human-readable result of the last list/apply call. */
const char *wimg_status(void);

#endif
