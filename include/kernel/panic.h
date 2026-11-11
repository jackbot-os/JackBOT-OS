#ifndef JACKBOT_PANIC_H
#define JACKBOT_PANIC_H

#include <stdint.h>

struct trap_frame;

/* Full stop screen: registers, fault address, last stage marker, serial dump,
   auto reboot. Never returns. */
void panic_screen(struct trap_frame *f, const char *why) __attribute__((noreturn));
void panic_stop(const char *why) __attribute__((noreturn));

/* Records which subsystem the main loop is inside right now, so a hang can
   name the place instead of freezing silently. */
void panic_stage(const char *s);

/* Main-loop heartbeat, called once per desktop iteration. */
void panic_beat(void);

/* Called from the watchdog thread: if the main loop has not beat for too
   long, show the stop screen instead of hanging forever. */
void panic_check_stall(void);

#endif
