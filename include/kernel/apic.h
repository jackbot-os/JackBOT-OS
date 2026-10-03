#ifndef JACKBOT_APIC_H
#define JACKBOT_APIC_H

#include <stdint.h>

void apic_init(void);
void apic_timer_init(void);
void apic_timer_reset(void);
void apic_eoi(void);

#endif
