#ifndef JACKBOT_HWCOMPAT_H
#define JACKBOT_HWCOMPAT_H

/* Machine profile + driver coverage report (hardware compatibility layer). */
void hwcompat_detect(void);
void hwcompat_report(void);
void hwcompat_self_test(void);

const char *hwcompat_hypervisor(void);
const char *hwcompat_profile(void);

#endif
