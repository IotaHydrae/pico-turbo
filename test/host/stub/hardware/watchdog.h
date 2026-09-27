/* Host stub for hardware/watchdog.h. */
#ifndef _HARDWARE_WATCHDOG_H
#define _HARDWARE_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

void watchdog_enable(uint32_t delay_ms, bool pause_on_debug);
void watchdog_update(void);
void watchdog_disable(void);
bool watchdog_caused_reboot(void);
bool watchdog_enable_caused_reboot(void);

/*: Host-side helpers: was it armed, and what timeout was it given. */
bool test_watchdog_armed(void);
uint32_t test_watchdog_delay_ms(void);
void test_watchdog_reset(void);

#endif
