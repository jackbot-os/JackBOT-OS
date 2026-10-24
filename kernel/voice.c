#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <kernel/voice.h>
#include <kernel/ac97.h>
#include <kernel/jbot.h>
#include <kernel/serial.h>

uint64_t timer_get_ticks(void);

static int state;
static int level;
static int peak;
static int heard;
static uint64_t listen_end;
static char reply[VOICE_REPLY_MAX + 1];

int voice_init(void)
{
    state = VOICE_IDLE;
    level = 0;
    peak = 0;
    heard = 0;
    reply[0] = 0;
    if (ac97_present()) {
        ac97_mic_init();
        serial_puts("[VOICE] mic channel ");
        serial_puts(ac97_mic_running() ? "running\n" : "unavailable\n");
        ac97_tone(880, 70);
    } else {
        serial_puts("[VOICE] no AC97, tones/mic disabled\n");
    }
    return ac97_present();
}

int voice_state(void) { return state; }
int voice_level(void) { return level; }
int voice_peak(void) { return peak; }

int voice_listen_remaining(void)
{
    if (state != VOICE_LISTEN)
        return 0;
    uint64_t now = timer_get_ticks();
    if (now >= listen_end)
        return 0;
    return (int)(listen_end - now);
}

void voice_listen_start(void)
{
    peak = 0;
    heard = 0;
    level = 0;
    if (ac97_present())
        ac97_tone(880, 60);
    if (!ac97_mic_running())
        ac97_mic_init();
    state = VOICE_LISTEN;
    listen_end = timer_get_ticks() + VOICE_LISTEN_TICKS;
    serial_puts("[VOICE] listening...\n");
}

void voice_listen_cancel(void)
{
    state = VOICE_IDLE;
    if (ac97_present())
        ac97_tone(440, 50);
}

void voice_tick(void)
{
    if (state != VOICE_LISTEN)
        return;

    int lvl = ac97_mic_level();
    level = lvl >= 0 ? lvl : 0;
    if (level > peak)
        peak = level;
    if (level >= VOICE_VAD_THRESHOLD)
        heard = 1;

    if (timer_get_ticks() >= listen_end) {
        if (heard) {
            state = VOICE_HEARD;
            serial_puts("[VOICE] speech detected, recognizer pending\n");
            if (ac97_present())
                ac97_tone(660, 60);
        } else {
            state = VOICE_NOSPEECH;
            serial_puts("[VOICE] no speech detected\n");
            if (ac97_present())
                ac97_tone(330, 80);
        }
    }
}

int voice_submit(const char *text)
{
    if (!text || !text[0])
        return 0;
    char buf[VOICE_REPLY_MAX + 1];
    if (jbot_reply(text, buf, sizeof(buf))) {
        int p = 0;
        while (buf[p] && p < VOICE_REPLY_MAX) {
            reply[p] = buf[p];
            p++;
        }
        reply[p] = 0;
        if (ac97_present())
            ac97_tone(740, 50);
        state = VOICE_IDLE;
        return 1;
    }
    return 0;
}

const char *voice_reply(void)
{
    return reply;
}

void voice_self_test(void)
{
    int ok = 1;

    if (!voice_submit("calc 9*7") || !strstr(reply, "63"))
        ok = 0;
    if (!voice_submit("time") || reply[0] == 0)
        ok = 0;

    /* window state machine: an already-expired window ends as no-speech */
    state = VOICE_LISTEN;
    listen_end = timer_get_ticks();
    voice_tick();
    if (state != VOICE_NOSPEECH)
        ok = 0;
    state = VOICE_IDLE;

    serial_puts(ok ? "[VOICE] selftest PASS\n" : "[VOICE] selftest FAIL\n");
}
