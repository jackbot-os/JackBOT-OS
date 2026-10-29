#ifndef KERNEL_PAYLOAD_H
#define KERNEL_PAYLOAD_H
#include <stdint.h>

#define PAYLOAD_BOOTIMG   "BOOTIMG"
#define PAYLOAD_COREIMG   "COREIMG"
#define PAYLOAD_GRUBMODS  "GRUBMODS"
#define PAYLOAD_GRUBCFG   "GRUBCFG"
#define PAYLOAD_FONT      "FONT"
#define PAYLOAD_SYSKERNEL "SYSKERNEL"
#define PAYLOAD_FSANS     "FSANS"
#define PAYLOAD_FMONO     "FMONO"
#define PAYLOAD_FMONOB    "FMONOB"
#define PAYLOAD_FCJK      "FCJK"
#define PAYLOAD_FKR       "FKR"
#define PAYLOAD_FAR       "FAR"
#define PAYLOAD_FTH       "FTH"
#define PAYLOAD_FHI       "FHI"
#define PAYLOAD_BOOTPCM   "BOOTPCM"
#define PAYLOAD_WARNPCM   "WARNPCM"

struct payload_mod {
    const char *name;
    const void *data;
    uint32_t size;
};

void payload_collect(uint64_t mb_info);
const struct payload_mod *payload_find(const char *name);
int payload_live_mode(void);
int payload_live_desktop(void);
int payload_fattest(void);
int payload_count(void);

#endif
