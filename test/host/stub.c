/*
 * The pico-sdk surface the library uses, implemented on the host so the parts of
 * pico-turbo that are policy -- the ladder, the tier table, the order the
 * regulator and the clock are touched in -- can be tested without a chip.
 *
 * Everything the library can do to the hardware is recorded rather than
 * performed, because the tests are about the sequence, not about the effect.
 */

#include "hardware/clocks.h"
#include "hardware/structs/watchdog.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- clocks ---- */

static uint32_t clock_hz[CLK_COUNT] = {
	[clk_sys] = 125000000u,
	[clk_peri] = 125000000u,
	[clk_usb] = 48000000u,
};

/*: The highest frequency the fake PLL can produce. */
static uint32_t pll_max_khz = 420000u;

uint32_t clock_get_hz(enum clock_index clk_index)
{
	return clock_hz[clk_index];
}

void test_clock_set_hz(enum clock_index clk_index, uint32_t hz)
{
	clock_hz[clk_index] = hz;
}

void test_pll_set_max_khz(uint32_t khz)
{
	pll_max_khz = khz;
}

bool check_sys_clock_khz(uint32_t freq_khz, uint *vco_out, uint *postdiv1_out,
                         uint *postdiv2_out)
{
	(void)postdiv2_out;

	if (freq_khz == 0 || freq_khz > pll_max_khz) {
		return false;
	}

	if (vco_out) {
		*vco_out = freq_khz * 12u;
	}
	if (postdiv1_out) {
		*postdiv1_out = 6u;
	}

	return true;
}

bool set_sys_clock_khz(uint32_t freq_khz, bool required)
{
	if (!check_sys_clock_khz(freq_khz, NULL, NULL, NULL)) {
		if (required) {
			return false;
		}
		/* The real SDK leaves the clock alone when it cannot hit the
		 * frequency, which is the behaviour the library relies on. */
		return false;
	}

	test_call_log_add("set_sys_clock_khz(%u)", freq_khz);
	clock_hz[clk_sys] = freq_khz * 1000u;
	/* The SDK re-points clk_peri at clk_sys as part of this. */
	clock_hz[clk_peri] = clock_hz[clk_sys];

	return true;
}

bool clock_configure_undivided(enum clock_index clk_index, uint32_t src,
                               uint32_t auxsrc, uint32_t src_freq)
{
	(void)src;
	(void)auxsrc;

	test_call_log_add("clock_configure_undivided(%d)", (int)clk_index);
	clock_hz[clk_index] = src_freq;

	return true;
}

/* ---- regulator ---- */

static enum vreg_voltage vreg = VREG_VOLTAGE_DEFAULT;

void vreg_set_voltage(enum vreg_voltage voltage)
{
	test_call_log_add("vreg_set_voltage(%d)", (int)voltage);
	vreg = voltage;
}

enum vreg_voltage vreg_get_voltage(void)
{
	return vreg;
}

void vreg_disable_voltage_limit(void)
{
	test_call_log_add("vreg_disable_voltage_limit()");
}

/* ---- watchdog ---- */

static watchdog_hw_t watchdog = { 0 };
watchdog_hw_t *watchdog_hw = &watchdog;

static bool armed;
static uint32_t delay_ms;

void watchdog_enable(uint32_t ms, bool pause_on_debug)
{
	(void)pause_on_debug;
	armed = true;
	delay_ms = ms;
}

void watchdog_update(void)
{
}

void watchdog_disable(void)
{
	armed = false;
}

bool watchdog_caused_reboot(void)
{
	return false;
}

bool watchdog_enable_caused_reboot(void)
{
	return false;
}

bool test_watchdog_armed(void)
{
	return armed;
}

uint32_t test_watchdog_delay_ms(void)
{
	return delay_ms;
}

void test_watchdog_reset(void)
{
	memset(&watchdog, 0, sizeof(watchdog));
	armed = false;
	delay_ms = 0;
}

/* ---- time ---- */

static uint64_t now_us;

uint64_t time_us_64(void)
{
	/* Time has to move on its own: the library's soak loops are "work until the
	 * deadline", and a clock that only moves when something waits would spin
	 * forever.  A readable clock that ticks is also what the real one is. */
	now_us += 10u;

	return now_us;
}

void test_time_advance_us(uint64_t us)
{
	now_us += us;
}

unsigned test_busy_wait_calls;

void busy_wait_at_least_cycles(uint32_t cycles)
{
	test_busy_wait_calls++;
	/* A real wait would be cycles / clk_sys seconds: on the host that is how the
	 * soak loop's deadline moves forward. */
	now_us += (uint64_t)cycles * 1000000u / (clock_hz[clk_sys] ? clock_hz[clk_sys] : 1u);
}

/* ---- the call log ---- */

/*: A climb of a few hundred MHz at a 5 MHz step is tens of candidates, each
 *: with a regulator and a clock write: the log has to hold a whole search, or
 *: an assertion about the end of one would be about a truncated log. */
#define CALL_LOG_MAX 1024

static char call_log[CALL_LOG_MAX][64];
static unsigned call_log_len;

void test_call_log_add(const char *fmt, ...)
{
	va_list ap;

	if (call_log_len >= CALL_LOG_MAX) {
		return;
	}

	va_start(ap, fmt);
	vsnprintf(call_log[call_log_len], sizeof(call_log[0]), fmt, ap);
	va_end(ap);
	call_log_len++;
}

unsigned test_call_log_count(void)
{
	return call_log_len;
}

const char *test_call_log_get(unsigned index)
{
	return index < call_log_len ? call_log[index] : "";
}

int test_call_log_find(const char *needle)
{
	for (unsigned i = 0; i < call_log_len; i++) {
		if (strstr(call_log[i], needle)) {
			return (int)i;
		}
	}

	return -1;
}

void test_call_log_clear(void)
{
	call_log_len = 0;
}

/* ---- flash, for the self-test's image hash ---- */

/*: The image the self-test hashes.  On the host that is a buffer of its own; all
 *: the test needs from it is that it does not change under it. */
char __flash_binary_start[4096] = { 1, 2, 3, 4 };
char __flash_binary_end[1];
