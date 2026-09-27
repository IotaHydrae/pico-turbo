/* Host stub for hardware/structs/watchdog.h: just the scratch registers. */
#ifndef _HARDWARE_STRUCTS_WATCHDOG_H
#define _HARDWARE_STRUCTS_WATCHDOG_H

#include <stdint.h>

typedef struct {
	uint32_t scratch[8];
} watchdog_hw_t;

extern watchdog_hw_t *watchdog_hw;

#endif
