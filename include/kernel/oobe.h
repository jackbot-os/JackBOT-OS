#ifndef KERNEL_OOBE_H
#define KERNEL_OOBE_H

/* First-boot out-of-box experience: language, keyboard layout, time zone,
   then welcome. Runs on the installed system before the desktop. It writes
   the choices to JBOT.CFG via config. */
void oobe_run(void);

#endif
