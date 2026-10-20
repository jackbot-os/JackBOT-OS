#ifndef JACKBOT_JBOT_H
#define JACKBOT_JBOT_H
#include <stdint.h>

enum { TIER_QUICK = 0, TIER_INITIAL = 1, TIER_DAILY = 2, TIER_PRO = 3 };
enum { CAP_DOC = 0, CAP_SCRIPT = 1, CAP_SYSTEM = 2, CAP_IMAGE = 3 };

void jbot_init(void);
int jbot_tier(void);
void jbot_set_tier(int t);
int jbot_can(int cap);
int jbot_reply(const char *input, char *out, int outsz);
void jbot_self_test(void);

#endif
