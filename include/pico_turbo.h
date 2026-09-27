/*
 * pico_turbo - clock and core-voltage control for RP2040 / RP2350
 *
 * Three layers, so a project can use as much or as little as it needs:
 *
 *   mechanism   pico_turbo_apply()      set a frequency and a voltage, safely
 *   self-check  pico_turbo_self_test()  does this chip still compute correctly?
 *   policy      pico_turbo_init()       the build-time answer (fixed or searched)
 *               pico_turbo_autotune()   search for the best answer, now
 *
 * Quick start (fixed frequency, chosen at build time):
 *
 *   cmake -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
 *
 *   #include "pico_turbo.h"
 *   int main(void) {
 *       pico_turbo_init();      // first thing in main(), before any peripheral
 *       ...
 *   }
 *
 * Quick start (let the chip decide, since silicon is not uniform):
 *
 *   cmake -DPICO_TURBO_SYS_CLK_KHZ=125000 -DPICO_TURBO_AUTOTUNE=1 ..
 *
 *   with autotuning on, PICO_TURBO_SYS_CLK_KHZ is the *starting* frequency and
 *   pico_turbo_init() walks up from there, stress-testing each candidate and
 *   keeping the best one that passes.  pico_turbo_state() reports what it found
 *   and pico_turbo_trace() lists every configuration it tried.
 *
 * Nothing else follows the clock for you: anything whose timing is fixed at
 * compile time from clk_sys or clk_peri (a PIO clock divider, a hard-coded SPI
 * baud rate, a PWM wrap) has to be recomputed afterwards, or built from
 * pico_turbo_state().  USB is the exception -- it runs from its own 48 MHz PLL,
 * which is why pico_turbo_state() reports it separately.
 */

#ifndef PICO_TURBO_H
#define PICO_TURBO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A frequency and the core voltage to run it at. */
typedef struct {
	uint32_t khz; /**< requested clk_sys, in kHz */
	uint16_t vreg_sel; /**< VREG_VOLTAGE_* value (the register encoding) */
} pico_turbo_config_t;

/** What the chip is doing now, and what the library had to do to get there. */
typedef struct {
	bool enabled; /**< built with a target frequency */
	bool tuned; /**< the configuration came from the runtime search */
	bool reached; /**< clk_sys matches what was requested, exactly */
	bool usb_ok; /**< clk_usb is still 48 MHz, so USB still works */
	uint32_t requested_khz; /**< what was asked for (the search result when tuned) */
	uint32_t sys_clk_khz; /**< clk_sys now */
	uint32_t peri_clk_khz; /**< clk_peri now */
	uint32_t usb_clk_khz; /**< clk_usb now */
	uint32_t flash_clk_khz; /**< clk_sys / PICO_FLASH_SPI_CLKDIV (0 if unknown) */
	uint16_t vreg_sel; /**< regulator setting read back, VREG_VOLTAGE_* encoding */
	uint32_t hangs; /**< candidates the watchdog had to reset during a search */
} pico_turbo_state_t;

/** Result of one configuration a search tried. */
typedef enum {
	PICO_TURBO_STEP_STABLE = 0, /**< passed the self-check */
	PICO_TURBO_STEP_FAILED = 1, /**< ran, but the self-check disagreed */
	PICO_TURBO_STEP_HANG = 2, /**< had to be reset by the watchdog */
	PICO_TURBO_STEP_SKIPPED =
		3, /**< the PLL could not go higher at this step */
} pico_turbo_step_result_t;

/** One entry of the search trace. */
typedef struct {
	uint32_t khz; /**< clk_sys that was reached */
	uint16_t vreg_sel; /**< regulator setting used */
	uint8_t result; /**< a pico_turbo_step_result_t */
} pico_turbo_step_t;

/** Entries the trace can hold; a search stops recording after that. */
#ifndef PICO_TURBO_TRACE_STEPS
#define PICO_TURBO_TRACE_STEPS 64
#endif

/** Returned by pico_turbo_self_test() when a soak round disagreed with the
 *  earlier ones: never a passing result, so a comparison against a reference
 *  taken on a good chip fails. */
#define PICO_TURBO_SELF_TEST_BAD 0xdeadbeefu

/** How far a search may go, and how hard it looks.  Zero fields take the
 *  compile-time defaults from the CMake configuration. */
typedef struct {
	uint32_t base_khz; /**< start here: must be known good on this chip */
	uint32_t max_khz; /**< never try anything above this */
	uint32_t step_khz; /**< frequency step */
	uint32_t stress_ms; /**< how long each candidate is stressed */
	uint16_t min_vreg_sel; /**< voltage floor (0 = whatever the table says) */
	uint16_t max_vreg_sel; /**< voltage ceiling (0 = the platform maximum) */
} pico_turbo_autotune_t;

/* ------------------------------------------------------------------ */
/*  Policy                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief Apply the configuration this build asked for.
 *
 * Call once at the very start of main(), before any peripheral setup.  Without
 * PICO_TURBO_AUTOTUNE that is one fixed frequency; with it, this runs the search
 * (see pico_turbo_autotune()).  When overclocking is disabled in the build this
 * does nothing, so the call site can stay in place for stock and turbo builds
 * alike.  A second call does nothing.
 */
void pico_turbo_init(void);

/**
 * @brief Search for the highest frequency this chip runs correctly.
 *
 * Walks the frequency ladder upward from @c base_khz, raising the voltage as
 * needed, self-checking every candidate, and leaves the chip at the best one that
 * passed.  Pass NULL for the compile-time defaults (which is what
 * pico_turbo_init() does when PICO_TURBO_AUTOTUNE is on).
 *
 * A candidate that hangs is caught by the watchdog and recorded in the trace
 * instead of ending the run; the result is kept in the watchdog scratch
 * registers, so it survives a warm reset but not a power cycle.
 *
 * @return the configuration now in effect
 */
pico_turbo_config_t pico_turbo_autotune(const pico_turbo_autotune_t *policy);

/* ------------------------------------------------------------------ */
/*  Mechanism                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief Apply a frequency and a voltage, in the order that is safe.
 *
 * Voltage first, then a settle delay, then the clock, then clk_peri.  This is
 * what a caller wants when it has its own idea of what the chip should run at.
 *
 * @return true when clk_sys came out exactly at @c config->khz
 */
bool pico_turbo_apply(const pico_turbo_config_t *config);

/** @brief The clocks and the configuration in effect. */
pico_turbo_state_t pico_turbo_state(void);

/** @brief The lowest voltage the library's table recommends for a frequency. */
uint16_t pico_turbo_voltage_for_khz(uint32_t khz);

/* ------------------------------------------------------------------ */
/*  Self-check                                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Run the self-checking workload for @c ms and return its hash.
 *
 * The workload mixes integer arithmetic (including divisions, so the divider is
 * exercised), SRAM traffic and a hash of a region of the image read back through
 * XIP -- the last part is what shows that the flash divider still works at this
 * frequency.  Every input is fixed, so the hash is a function of the work done
 * alone: take it once at a configuration known to be good, then compare after
 * every change.  A chip that runs but computes wrong answers fails this, which a
 * crash test would not notice.
 */
uint32_t pico_turbo_self_test(uint32_t ms);

/**
 * @brief Run the self-check and compare it with a reference hash.
 *
 * @return true when the chip still produces the same answers
 */
bool pico_turbo_self_check(uint32_t ms, uint32_t reference);

/* ------------------------------------------------------------------ */
/*  Tiers: stable configurations to choose from                        */
/* ------------------------------------------------------------------ */

/**
 * @brief How many tiers a pico_turbo_config_t array for one chip can hold.
 *
 * One per core-voltage step plus the starting configuration is what a search
 * usually produces, so this is small on purpose: a tier table is meant to be a
 * handful of choices, not a dump of every frequency that was tried.
 */
#ifndef PICO_TURBO_TIERS_MAX
#define PICO_TURBO_TIERS_MAX 8
#endif

/**
 * @brief Hand the library a table of configurations to choose from.
 *
 * Typical source of such a table: the header generated by examples/tune, which
 * holds the stable points measured on the chip the program is running on.  The
 * table is kept by pointer, so it has to stay alive; usually it is `static const`.
 */
void pico_turbo_use_table(const pico_turbo_config_t *configs, uint32_t count);

/** @brief How many tiers the current table has (0 when none was given). */
uint32_t pico_turbo_tier_count(void);

/** @brief Tier @c tier of the table, or a zeroed configuration when out of range. */
pico_turbo_config_t pico_turbo_tier(uint32_t tier);

/**
 * @brief Apply tier @c tier and remember it as the configuration in effect.
 *
 * Safe to call at any time, including long after startup -- this is how an
 * application exposes "run fast" / "run cool" as a choice.
 *
 * @return true when the tier exists and clk_sys came out exactly at its frequency
 */
bool pico_turbo_select(uint32_t tier);

/**
 * @brief Turn the last search's trace into a tier table.
 *
 * Keeps the highest frequency that passed at each core voltage, which is what
 * makes a table worth having: one entry is the fastest the chip does at its
 * lowest voltage (cool and thrifty), the next is what one voltage step buys, and
 * so on up to the last one, which is the fastest at the highest voltage that was
 * tried.  The starting configuration is always entry 0, so a caller can pick
 * "tier 0" and be back where it began.
 *
 * @param out table to fill
 * @param max entries @c out can hold
 * @return the number of entries written
 */
uint32_t pico_turbo_tiers_from_trace(pico_turbo_config_t *out, uint32_t max);

/* ------------------------------------------------------------------ */
/*  Reporting                                                         */
/* ------------------------------------------------------------------ */

/**
 * @brief The configurations the last search tried, oldest first.
 *
 * @param steps receives a pointer to the trace (never NULL)
 * @return the number of entries, 0 when no search has run
 */
uint32_t pico_turbo_trace(const pico_turbo_step_t **steps);

#ifdef __cplusplus
}
#endif

#endif /* PICO_TURBO_H */
