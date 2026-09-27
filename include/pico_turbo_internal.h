/*
 * pico_turbo - what the library's own translation units share
 *
 * Not part of the public API: a project linking the library only includes
 * pico_turbo.h.
 */

#ifndef PICO_TURBO_INTERNAL_H
#define PICO_TURBO_INTERNAL_H

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico_turbo.h"

/*: How long to let the regulator settle before the clock changes: twice the
 *: SDK's own minimum, which is what it waits when it adjusts the voltage
 *: itself. */
#define PICO_TURBO_SETTLE_US (SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US * 2)

/*: Highest regulator setting a search may use.  The hardware allows more on the
 *: RP2350 (up to 3.30 V), but nothing here is worth cooking a chip for, so the
 *: ceiling is the platform's documented maximum.  Raise it deliberately if you
 *: know what your cooling can take. */
#ifndef PICO_TURBO_MAX_VREG_VOLTAGE
#if defined(PICO_RP2040)
#define PICO_TURBO_MAX_VREG_VOLTAGE VREG_VOLTAGE_1_30
#else
#define PICO_TURBO_MAX_VREG_VOLTAGE VREG_VOLTAGE_1_60
#endif
#endif

#define PICO_TURBO_MAX_VREG_SEL ((uint16_t)PICO_TURBO_MAX_VREG_VOLTAGE)

/*: The base frequency the build was configured with (the fixed target, or where a
 *: search starts).  Undefined in a build without overclocking. */
#ifdef PICO_TURBO_SYS_CLK_KHZ
#define PICO_TURBO_BASE_CLK_KHZ PICO_TURBO_SYS_CLK_KHZ
#endif

/*: Where a search stops when the caller does not say. */
#ifndef PICO_TURBO_MAX_CLK_KHZ
#define PICO_TURBO_MAX_CLK_KHZ 420000u
#endif

/*: Busy-wait for a real microsecond delay (converted with the clock running now,
 *: because that is what busy_wait_at_least_cycles() counts). */
void pico_turbo_delay_us(uint32_t us);

/*: Fill in and publish the state: the clocks as they are, the configuration that
 *: was just applied, and whether it came from a search. */
void pico_turbo_publish(const pico_turbo_config_t *config, bool tuned,
			uint32_t hangs);

/*: The trace of the last search; the tuner owns the array. */
void pico_turbo_trace_add(uint32_t khz, uint16_t vreg_sel, uint8_t result);

/*: Start a new trace.  A search describes itself, not every search this
 *: power-up has run: appending would make "the trace of the last search" mean
 *: "of all of them". */
void pico_turbo_trace_reset(void);

#endif /* PICO_TURBO_INTERNAL_H */
