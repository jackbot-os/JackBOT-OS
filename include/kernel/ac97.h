#ifndef JACKBOT_AC97_H
#define JACKBOT_AC97_H

#include <stdint.h>
#include <stddef.h>

#define AC97_RATE 48000

int ac97_init(void);
int ac97_present(void);
void ac97_play_pcm(const void *data, uint32_t bytes);
void ac97_play_boot(void);
void ac97_play_warn(void);

#endif
