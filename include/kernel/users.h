#ifndef KERNEL_USERS_H
#define KERNEL_USERS_H

#define USERS_MAX 8
#define USER_NAME_MAX 16

/* Accounts are a real, persistent list (USERS.CFG on the data volume). A
   password is optional: hash 0 means "no password", which is also the first
   boot state so the machine is usable before the owner sets one. */
void users_init(void);
int users_count(void);
const char *users_name(int i);
int users_has_pass(int i);
int users_current(void);
int users_auth(int i, const char *pass);   /* 1 = password ok */
int users_switch(int i);
int users_add(const char *name, const char *pass); /* new index, or -1 */
int users_set_pass(int i, const char *pass);
/* "HOME/<NAME>" for user i; the per-user writable area. */
void users_home(int i, char *out, int osz);
/* Create SYSTEM + HOME + one folder per user (no-op without a volume). */
void users_ensure_folders(void);
void users_save(void);
/* Blocks on the password prompt when the current account has one. */
void login_run(void);

#endif
