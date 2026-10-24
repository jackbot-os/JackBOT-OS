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

/* Synthesized cue tone (mono 16-bit sine, generated in place). */
void ac97_tone(int freq_hz, int ms);

/* Microphone capture channel (AC97 MC bus-master). Returns 1 if running.
   ac97_mic_level returns 0..100 RMS level, or -1 when capture is down. */
int ac97_mic_init(void);
int ac97_mic_running(void);
int ac97_mic_level(void);

#endif
