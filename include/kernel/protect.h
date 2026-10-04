#ifndef KERNEL_PROTECT_H
#define KERNEL_PROTECT_H

#define ACTOR_USER 0
#define ACTOR_ADMIN 1
#define ACTOR_JBOT 2

#define ACT_READ 0
#define ACT_WRITE 1
#define ACT_DELETE 2

int protect_is_core(const char *name83);
int protect_check(const char *name83, int action);
int protect_set_actor(int actor);
int protect_get_actor(void);

#endif
