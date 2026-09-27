/*
 * Host tests for the parts of pico-turbo that are policy rather than silicon.
 *
 * The chip-dependent half of the library -- whether this particular RP2040 does
 * 420 MHz -- can only be answered on hardware.  What can be answered here is
 * everything that would still be wrong on a chip that does: the order the
 * regulator and the clock are touched in, whether a configuration that is not a
 * configuration is refused, and what a search's trace reduces to.
 *
 * The order is the one that matters most.  Lowering the voltage while the core
 * is still running at the frequency that needed the old one is how a board ends
 * up locked up and needing to be unplugged: it was measured doing exactly that,
 * which is what this file is here to keep from coming back.
 */

#include "test.h"

#include "hardware/clocks.h"
#include "hardware/structs/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

#include "pico_turbo.h"
#include "pico_turbo_internal.h"

#include <stdio.h>
#include <string.h>

static unsigned failures;
static unsigned checks;

static void check(bool ok, const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("  FAIL %s\n", what);
	}
}

static void check_eq_u32(uint32_t got, uint32_t want, const char *what)
{
	checks++;
	if (got != want) {
		failures++;
		printf("  FAIL %s: got %lu, want %lu\n", what, (unsigned long)got,
		       (unsigned long)want);
	}
}

/*: Put the fake chip into a known good state: stock clocks, default regulator,
 *: empty call log, empty scratch. */
static void reset_chip(void)
{
	test_call_log_clear();
	test_watchdog_reset();
	test_time_advance_us(0);
	test_pll_set_max_khz(420000);
	test_clock_set_hz(clk_sys, 125000000u);
	test_clock_set_hz(clk_peri, 125000000u);
	test_clock_set_hz(clk_usb, 48000000u);
	vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
	test_call_log_clear();
}

static void test_lowering_takes_the_clock_down_first(void)
{
	int clock_at, voltage_at;
	pico_turbo_config_t low = { .khz = 130000u, .vreg_sel = 11u };

	printf("lowering: the clock comes down before the supply does\n");
	reset_chip();
	test_clock_set_hz(clk_sys, 420000000u);
	vreg_set_voltage(VREG_VOLTAGE_1_30);
	test_call_log_clear();

	check(pico_turbo_apply(&low), "apply reports the frequency was reached exactly");

	clock_at = test_call_log_find("set_sys_clock_khz(130000)");
	voltage_at = test_call_log_find("vreg_set_voltage(11)");

	check(clock_at >= 0, "the clock was lowered");
	check(voltage_at >= 0, "the regulator was lowered");
	check(clock_at < voltage_at,
	      "the core was off the old frequency before the supply was cut");

	/* And the other way round: raising has to do it in the opposite order, or the
	 * core is briefly asked to run faster than the regulator supplies. */
	printf("raising: the supply comes up before the clock does\n");
	reset_chip();
	{
		pico_turbo_config_t high = { .khz = 300000u, .vreg_sel = 13u };

		test_clock_set_hz(clk_sys, 130000000u);
		vreg_set_voltage(VREG_VOLTAGE_1_10);
		test_call_log_clear();

		check(pico_turbo_apply(&high), "apply reports the frequency was reached exactly");

		clock_at = test_call_log_find("set_sys_clock_khz(300000)");
		voltage_at = test_call_log_find("vreg_set_voltage(13)");

		check(clock_at >= 0 && voltage_at >= 0, "both were changed");
		check(voltage_at < clock_at,
		      "the supply was there before the clock went up");
	}
}

static void test_apply_refuses_what_is_not_a_configuration(void)
{
	pico_turbo_config_t junk = { .khz = 235479u, .vreg_sel = 13440u };
	pico_turbo_config_t zero = { .khz = 0u, .vreg_sel = 11u };
	pico_turbo_config_t too_high = { .khz = 500000u, .vreg_sel = 15u };

	printf("a table entry that is not a configuration is refused, not applied\n");
	reset_chip();

	check(!pico_turbo_apply(&junk), "a garbage regulator setting is refused");
	check_eq_u32(test_call_log_count(), 0,
	             "nothing was written to the regulator or the clock");

	check(!pico_turbo_apply(&zero), "a zero frequency is refused");
	check(!pico_turbo_apply(&too_high), "a frequency above the build maximum is refused");
	check_eq_u32(test_call_log_count(), 0, "still nothing was touched");
}

static void test_tiers_are_the_best_at_each_voltage(void)
{
	static pico_turbo_config_t tiers[PICO_TURBO_TIERS_MAX];
	uint32_t count;

	printf("a trace reduces to the fastest stable frequency per voltage\n");
	reset_chip();

	/* Climb order, as a search produces it: two steps at sel 11, then a step up to
	 * sel 12 that fails, then a good one at sel 13 and an unachievable one. */
	pico_turbo_trace_add(130000u, 11u, PICO_TURBO_STEP_STABLE);
	pico_turbo_trace_add(140000u, 11u, PICO_TURBO_STEP_STABLE);
	pico_turbo_trace_add(150000u, 12u, PICO_TURBO_STEP_STABLE);
	pico_turbo_trace_add(160000u, 12u, PICO_TURBO_STEP_FAILED);
	pico_turbo_trace_add(170000u, 13u, PICO_TURBO_STEP_STABLE);
	pico_turbo_trace_add(180000u, 13u, PICO_TURBO_STEP_SKIPPED);

	count = pico_turbo_tiers_from_trace(tiers, PICO_TURBO_TIERS_MAX);

	check_eq_u32(count, 3u, "one tier per voltage that had a stable frequency");
	check_eq_u32(tiers[0].khz, 140000u, "tier 0 keeps the best at sel 11");
	check_eq_u32(tiers[0].vreg_sel, 11u, "tier 0 voltage");
	check_eq_u32(tiers[1].khz, 150000u, "tier 1 keeps the best at sel 12");
	check_eq_u32(tiers[1].vreg_sel, 12u, "tier 1 voltage");
	check_eq_u32(tiers[2].khz, 170000u, "tier 2 keeps the best at sel 13");
	check_eq_u32(tiers[2].vreg_sel, 13u, "tier 2 voltage");
}

static void test_selection_refuses_a_bad_tier(void)
{
	static const pico_turbo_config_t table[] = {
		{ 235479u, 13440u }, /* what a corrupt record looks like */
	};

	printf("selecting from a table with a corrupt entry changes nothing\n");
	reset_chip();
	pico_turbo_use_table(table, 1);

	check(!pico_turbo_select(0), "the corrupt tier is refused");
	check_eq_u32(test_call_log_count(), 0, "the regulator was not touched");
}

static void test_a_search_stops_at_what_the_pll_can_do(void)
{
	pico_turbo_autotune_t policy = {
		.base_khz = 125000u,
		.max_khz = 200000u,
		.step_khz = 5000u,
		.stress_ms = 1u,
	};
	pico_turbo_config_t config;
	const pico_turbo_step_t *steps = NULL;
	uint32_t n;

	printf("a search climbs to the ceiling and stays inside it\n");
	reset_chip();
	test_pll_set_max_khz(180000u);

	config = pico_turbo_autotune(&policy);
	n = pico_turbo_trace(&steps);

	check(config.khz <= 180000u, "the result is inside what the PLL can produce");
	check(config.khz >= 125000u, "the result is not below where the search started");
	check(n > 0u, "the search left a trace");
	check(!test_watchdog_armed(), "the watchdog is not left armed after a search");
	check(steps != NULL, "the trace is readable");
}

int main(void)
{
	printf("pico-turbo host tests\n\n");

	test_lowering_takes_the_clock_down_first();
	printf("\n");
	test_apply_refuses_what_is_not_a_configuration();
	printf("\n");
	test_tiers_are_the_best_at_each_voltage();
	printf("\n");
	test_selection_refuses_a_bad_tier();
	printf("\n");
	test_a_search_stops_at_what_the_pll_can_do();

	printf("\n%u checks, %u failed\n", checks, failures);

	return failures == 0 ? 0 : 1;
}
