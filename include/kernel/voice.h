#ifndef JACKBOT_VOICE_H
#define JACKBOT_VOICE_H
/*
 * JackBOT OS voice assistant framework.
 *
 * Real in this stage:
 *   - AC97 microphone capture channel bring-up (MC bus-master DMA)
 *   - live RMS input level metering
 *   - simple energy-based voice activity detection (VAD) while listening
 *   - synthesized cue tones (start/end) over AC97
 *   - command dispatch to the offline Jbot engine (jbot_reply)
 *
 * Honest limitation:
 *   - there is no on-device speech-to-text engine in the kernel yet; until
 *     that lands, recognized commands are typed into the text box. The
 *     mic path, VAD and cue tones are fully functional, so when a compact
 *     recognizer is added it only has to turn audio into text.
 */
#include <stdint.h>

enum {
    VOICE_IDLE = 0,
    VOICE_LISTEN,
    VOICE_HEARD,
    VOICE_NOSPEECH,
};

#define VOICE_REPLY_MAX 160
#define VOICE_LISTEN_TICKS 200   /* 2 s window at 100 Hz */
#define VOICE_VAD_THRESHOLD 12

int voice_init(void);
int voice_state(void);
int voice_level(void);          /* 0..100 live mic level */
int voice_peak(void);           /* peak level during current listen window */
int voice_listen_remaining(void);

void voice_listen_start(void);
void voice_listen_cancel(void);
void voice_tick(void);

/* Feed a command (typed text today; recognizer output later) to Jbot. */
int voice_submit(const char *text);
const char *voice_reply(void);
void voice_self_test(void);

#endif
