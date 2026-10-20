#ifndef KERNEL_INSTALLER_H
#define KERNEL_INSTALLER_H
#include <stdint.h>
#include <kernel/part.h>

#define INSTALL_MARKER "JBINST.DAT"

#define INST_STATE_NO_DISK 0
#define INST_STATE_EMPTY 1
#define INST_STATE_INSTALLED 2
#define INST_STATE_FOREIGN 3

#define INST_MODE_FRESH 0
#define INST_MODE_UPGRADE 1

struct disk_info {
    uint64_t total_sectors;
    int state;
    struct part_layout layout;
    uint32_t sys_start;
    uint32_t sys_sectors;
};

typedef void (*install_progress_fn)(const char *step, int percent);

int installer_probe(struct disk_info *di);
int installer_install(int mode, install_progress_fn progress);
int installer_have_payload(void);
void installer_set_language(int idx);
int installer_language(void);
/* 0 = automatic partitioning; otherwise requested system size in MiB. */
void installer_set_sys_mb(uint32_t mb);
uint32_t installer_sys_mb(void);
/* Time zone index in config's 0..24 UTC offset table (hour = idx - 12). */
void installer_set_tz(int idx);
int installer_tz(void);

int postinstall_cleanup(void);

#endif
