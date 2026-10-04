#include <stdint.h>
#include <stddef.h>
#include <kernel/installer.h>
#include <kernel/fat32.h>
#include <kernel/serial.h>

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static const char sys_blob[] = "JackBOT OS 0.5.0 installed system image";
static const char stg_blob[] = "INSTALLER STAGING PAYLOAD - remove after install";

int installer_run(void)
{
    if (!fat_mounted())
        return -1;

    if (fat_write_file("SYSTEM.IMG", sys_blob, slen(sys_blob)) < 0)
        return -1;
    if (fat_write_file("INSTALL.STG", stg_blob, slen(stg_blob)) < 0)
        return -1;
    if (fat_write_file("FIRSTBOOT", "1", 1) < 0)
        return -1;

    serial_puts("[INST] staged system + installer payload\n");
    return 0;
}

int postinstall_cleanup(void)
{
    if (!fat_mounted())
        return -1;
    if (!fat_exists("FIRSTBOOT"))
        return 0;

    uint32_t before = fat_free_bytes();

    if (fat_exists("INSTALL.STG"))
        fat_delete("INSTALL.STG");
    fat_delete("FIRSTBOOT");

    uint32_t after = fat_free_bytes();
    int sys_ok = fat_exists("SYSTEM.IMG");

    serial_puts("[INST] post-install cleanup done, freed=");
    serial_puthex(after - before);
    serial_putc('\n');
    serial_puts(sys_ok ? "[INST] system image retained\n"
                       : "[INST] system image missing\n");
    return sys_ok ? 0 : -1;
}
