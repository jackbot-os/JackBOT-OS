/* Deterministic offline harness for the real kernel fat32.c.
   Local VM only: in-memory block device, no disk, no network. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <kernel/fat32.h>
#include <kernel/protect.h>
#include <kernel/persist.h>

extern void stub_disk_init(uint32_t sectors);
extern void stub_disk_format(void);

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("PASS: %s\n", name); } \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static int write_read_back(const char *path, const uint8_t *data, uint32_t len)
{
    if (fat_write_file(path, data, len) != 0) return -1;
    static uint8_t buf[20000];
    uint32_t got = 0;
    if (fat_read_file(path, buf, sizeof buf, &got) != 0) return -1;
    if (got != len) return -2;
    return memcmp(buf, data, len) == 0 ? 0 : -3;
}

int main(void)
{
    stub_disk_init(8192);
    stub_disk_format();

    CHECK(fat_mount_part(0, 8192) == 0, "mount formatted disk");
    CHECK(fat_mounted() == 1, "mounted flag set");

    uint32_t total = fat_total_bytes();
    uint32_t free0 = fat_free_bytes();
    CHECK(total > 0 && free0 > 0 && free0 <= total, "total/free bytes sane");

    /* 1. small file round trip */
    const char *msg = "JackBOT FAT32 round trip 0123456789";
    CHECK(write_read_back("A.TXT", (const uint8_t *)msg, (uint32_t)strlen(msg)) == 0,
          "small file write/readback");
    CHECK(fat_exists("A.TXT") == 1, "A.TXT exists");

    /* 2. overwrite reuses space; content updates */
    const char *msg2 = "overwritten content, longer than before";
    CHECK(write_read_back("A.TXT", (const uint8_t *)msg2, (uint32_t)strlen(msg2)) == 0,
          "overwrite A.TXT round trip");
    uint32_t sz = 0;
    CHECK(fat_file_size("A.TXT", &sz) == 0 && sz == strlen(msg2), "overwrite size updated");

    /* 3. multi-cluster file (> 8 sectors * 512 = 4096 bytes) */
    static uint8_t big[9000];
    for (uint32_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 31 + 7);
    CHECK(write_read_back("BIG.BIN", big, sizeof big) == 0, "multi-cluster file round trip");

    /* 4. mkdir + file inside subdirectory */
    CHECK(fat_mkdir("DIR") == 0, "mkdir DIR");
    CHECK(fat_dir_exists("DIR") == 1, "DIR is directory");
    const char *sub = "nested file inside directory";
    CHECK(write_read_back("DIR/F.TXT", (const uint8_t *)sub, (uint32_t)strlen(sub)) == 0,
          "subdirectory file round trip");

    /* 5. directory listing */
    struct fat_dirent dent[64];
    int n = fat_list_dir("", dent, 64);
    CHECK(n >= 2, "root listing has A.TXT/BIG.BIN/DIR");
    int saw_dir = 0, saw_a = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(dent[i].short83, "DIR") == 0 && dent[i].is_dir) saw_dir = 1;
        if (strcmp(dent[i].short83, "A.TXT") == 0) saw_a = 1;
    }
    CHECK(saw_dir, "listing shows DIR directory");
    CHECK(saw_a, "listing shows A.TXT");

    int ns = fat_list_dir("DIR", dent, 64);
    CHECK(ns == 1 && strcmp(dent[0].short83, "F.TXT") == 0, "DIR listing shows F.TXT");

    /* 6. delete frees space */
    uint32_t free_before_del = fat_free_bytes();
    CHECK(fat_delete("BIG.BIN") == 0, "delete BIG.BIN");
    CHECK(fat_exists("BIG.BIN") == 0, "BIG.BIN gone after delete");
    CHECK(fat_free_bytes() >= free_before_del, "free space recovered after delete");

    /* 7. read missing file fails cleanly */
    uint8_t rb[64]; uint32_t rg = 0;
    CHECK(fat_read_file("NOPE.TXT", rb, sizeof rb, &rg) != 0, "missing file read fails");

    /* 8. zero-byte file */
    CHECK(fat_write_file("EMPTY.DAT", "", 0) == 0, "write zero-byte file");
    uint32_t esz = 0;
    CHECK(fat_file_size("EMPTY.DAT", &esz) == 0 && esz == 0, "zero-byte size is 0");
    uint32_t eg = 0;
    CHECK(fat_read_file("EMPTY.DAT", rb, sizeof rb, &eg) == 0 && eg == 0,
          "zero-byte read returns length 0");

    /* 9. path too long rejected without crash */
    char longpath[128];
    memset(longpath, 'x', sizeof longpath - 1);
    longpath[sizeof longpath - 1] = 0;
    CHECK(fat_write_file(longpath, "z", 1) != 0, "overlong path rejected");

    /* 10. move file */
    CHECK(fat_move("A.TXT", "MOVED.TXT") == 0, "move A.TXT to MOVED.TXT");
    CHECK(fat_exists("A.TXT") == 0, "source gone after move");
    uint8_t mv[128]; uint32_t mg = 0;
    CHECK(fat_read_file("MOVED.TXT", mv, sizeof mv, &mg) == 0 &&
          mg == strlen(msg2) && memcmp(mv, msg2, mg) == 0, "moved content intact");

    /* 11. core file write blocked for USER, allowed for ADMIN */
    fat_delete("SYSTEM.IMG");
    fat_exists("SYSTEM.IMG");
    protect_set_actor(ACTOR_USER);
    CHECK(protect_get_actor() == ACTOR_USER, "actor set to USER");
    CHECK(fat_write_file("SYSTEM.IMG", "x", 1) != 0, "USER blocked writing core file");
    protect_set_actor(ACTOR_ADMIN);
    CHECK(protect_get_actor() == ACTOR_ADMIN, "actor set to ADMIN");
    CHECK(fat_write_file("SYSTEM.IMG", "core", 4) == 0, "ADMIN allowed writing core file");
    fat_delete("SYSTEM.IMG");

    /* 12. LFN long-name write under DIR, listing restores long name */
    CHECK(fat_write_file_lfn("DIR", "My Long File Name.TXT",
            (const uint8_t *)"lfn body", 9) == 0, "LFN write in subdir");
    struct fat_dirent dl[16];
    int nl = fat_list_dir("DIR", dl, 16);
    int saw_lfn = 0;
    for (int i = 0; i < nl; i++)
        if (strstr(dl[i].name, "My Long File Name") != 0)
            saw_lfn = 1;
    CHECK(saw_lfn, "listing shows LFN long name");

    protect_set_actor(ACTOR_USER);
    CHECK(protect_get_actor() == ACTOR_USER, "actor USER again");
    CHECK(fat_write_file_lfn("DIR", "SYSTEM.IMG", "x", 1) != 0,
          "USER blocked LFN write of core file");
    protect_set_actor(ACTOR_ADMIN);

    /* 13. unmount guards: every entry returns error, no crash */
    fat_unmount();
    CHECK(fat_mounted() == 0, "unmounted flag cleared");
    CHECK(fat_exists("A.TXT") < 0, "exists guarded when unmounted");
    CHECK(fat_read_file("A.TXT", rb, sizeof rb, &rg) != 0, "read guarded when unmounted");
    CHECK(fat_write_file("Z.TXT", "x", 1) != 0, "write guarded when unmounted");
    CHECK(fat_delete("Z.TXT") != 0, "delete guarded when unmounted");

    /* 14. tiny disk exhaustion: failed allocation rolls back, no leak/crash */
    stub_disk_init(128);
    stub_disk_format();
    CHECK(fat_mount_part(0, 128) == 0, "mount tiny disk");
    int accepted = 0;
    for (int i = 0; i < 256; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "F%03d.TXT", i);
        if (fat_write_file(nm, "0123456789", 10) == 0)
            accepted++;
        else
            break;
    }
    CHECK(accepted > 0, "tiny disk accepted some files before full");
    CHECK(fat_free_bytes() <= fat_total_bytes(), "free bytes sane after exhaustion");
    uint32_t free_after_fail = fat_free_bytes();
    for (int i = 0; i < 20; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "X%03d.TXT", i);
        fat_write_file(nm, "x", 1);
    }
    CHECK(fat_free_bytes() == free_after_fail, "failed writes do not leak clusters");
    fat_unmount();

    printf(failures == 0 ? "\nALL FAT32 HARNESS PASS\n" : "\nFAT32 HARNESS FAILURES=%d\n",
           failures);
    return failures == 0 ? 0 : 1;
}
