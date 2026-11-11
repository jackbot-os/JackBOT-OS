#ifndef JACKBOT_SESSION_H
#define JACKBOT_SESSION_H

/* Session snapshot written before power off: UI scale, glass mode and the
   clock at shutdown, saved as JBSESSN.TXT on the boot volume. Returns the
   number of bytes written, -1 when no volume is writable, -2 when the file
   cannot be read back. */
int session_save(void);

/* Reads the snapshot back into out (NUL terminated). Returns its length or -1. */
int session_load(char *out, int max);

#endif
