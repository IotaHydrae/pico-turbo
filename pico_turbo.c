/*
 * pico_turbo - the mechanism, and the answer this build was configured with
 *
 * Voltage / frequency guidance, from what these parts are known to do:
 *
 * RP2040:
 *   <= 266 MHz  ->  1.10 V  (default)
 *   <= 360 MHz  ->  1.20 V
 *   <= 396 MHz  ->  1.25 V
 *    > 396 MHz  ->  1.30 V  (maximum)
 *
 * RP2350:
 *   <= 150 MHz  ->  1.10 V  (default)
 *   <= 300 MHz  ->  1.20 V
 *   <= 384 MHz  ->  1.30 V
 *   <= 440 MHz  ->  1.40 V
 *   <= 500 MHz  ->  1.50 V
 *    > 500 MHz  ->  1.60 V
 *
 * These are the *starting points*, not the truth about a given chip: the table is
 * what a search begins from (see pico_turbo_tune.c) and what a fixed-frequency
 * build applies directly.
 */

#include "pico_turbo.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico.h"

#include "pico_turbo_internal.h"

/*: The lowest voltage each frequency range is known to work at.  The last entry
 *: has no upper bound and is the platform maximum. */
typedef struct {
	uint32_t max_mhz;
	enum vreg_voltage voltage;
} turbo_vreg_step_t;

#if defined(PICO_RP2040)
static const turbo_vreg_step_t turbo_vreg_table[] = {
	{ 266, VREG_VOLTAGE_DEFAULT },
	{ 360, VREG_VOLTAGE_1_20 },
	{ 396, VREG_VOLTAGE_1_25 },
	{ UINT32_MAX, VREG_VOLTAGE_1_30 },
};
#elif defined(PICO_RP2350)
static const turbo_vreg_step_t turbo_vreg_table[] = {
	{ 150, VREG_VOLTAGE_DEFAULT }, { 300, VREG_VOLTAGE_1_20 },
	{ 384, VREG_VOLTAGE_1_30 },    { 440, VREG_VOLTAGE_1_40 },
	{ 500, VREG_VOLTAGE_1_50 },    { UINT32_MAX, VREG_VOLTAGE_1_60 },
};
#else
#error "pico_turbo: unknown platform -- only RP2040 and RP2350 are supported"
#endif

#define TURBO_VREG_STEPS \
	(sizeof(turbo_vreg_table) / sizeof(turbo_vreg_table[0]))

static pico_turbo_state_t s_state;
static bool s_initialised;

uint16_t pico_turbo_voltage_for_khz(uint32_t khz)
{
#ifdef PICO_TURBO_VREG_VOLTAGE
	(void)khz;

	/* the build asked for one explicitly */
	return (uint16_t)PICO_TURBO_VREG_VOLTAGE;
#else
	uint32_t mhz = khz / 1000u;

	for (uint32_t i = 0; i < TURBO_VREG_STEPS; i++) {
		if (mhz <= turbo_vreg_table[i].max_mhz) {
			return (uint16_t)turbo_vreg_table[i].voltage;
		}
	}

	return (uint16_t)turbo_vreg_table[TURBO_VREG_STEPS - 1].voltage;
#endif
}

/*
 * busy_wait_at_least_cycles() counts *system* clock cycles, so a microsecond
 * delay has to be converted with the clock that is running now -- a PLL change
 * has not happened yet at the point this is used.  Converting with XOSC cycles
 * instead (12 MHz against a 125 MHz core, as this library used to) makes the wait
 * about ten times shorter than the delay it claims to implement.
 */
void pico_turbo_delay_us(uint32_t us)
{
	uint32_t cycles =
		(uint32_t)(((uint64_t)us * clock_get_hz(clk_sys)) / 1000000u);

	busy_wait_at_least_cycles(cycles);
}

void pico_turbo_publish(const pico_turbo_config_t *config, bool tuned,
			uint32_t hangs)
{
	uint32_t sys_khz = clock_get_hz(clk_sys) / 1000u;

	s_state.enabled = true;
	s_state.tuned = tuned;
	s_state.reached = (sys_khz == config->khz);
	s_state.requested_khz = config->khz;
	s_state.sys_clk_khz = sys_khz;
	s_state.peri_clk_khz = clock_get_hz(clk_peri) / 1000u;
	s_state.usb_clk_khz = clock_get_hz(clk_usb) / 1000u;
	s_state.usb_ok = (s_state.usb_clk_khz == 48000u);
	s_state.flash_clk_khz = 0;
#ifdef PICO_FLASH_SPI_CLKDIV
	/* What the application believes the flash is clocked at.  The boot stage 2 has
	 * to carry the same divider -- the CMake side patches it when it has to
	 * change it (see the comment there).  Reading a region of the image back in
	 * pico_turbo_self_test() is what proves the divider is actually safe. */
	s_state.flash_clk_khz = sys_khz / PICO_FLASH_SPI_CLKDIV;
#endif
	/* vreg_get_voltage() answers in the SDK's enum, which is the register
	 * encoding: VREG_VOLTAGE_1_30 is 0b1111, not 1300. */
	s_state.vreg_sel = (uint16_t)vreg_get_voltage();
	s_state.hangs = hangs;
}

bool pico_turbo_apply(const pico_turbo_config_t *config)
{
	enum vreg_voltage voltage = (enum vreg_voltage)config->vreg_sel;
	uint32_t sys_hz;

	/* Above the platform maximum the limit has to be lifted first (RP2350; on
	 * RP2040 nothing in this library's tables goes that high). */
	if (voltage > VREG_VOLTAGE_MAX) {
		vreg_disable_voltage_limit();
	}

	/* Voltage before clock, always: the other order asks the core to run at a
	 * frequency the regulator is not supplying yet. */
	vreg_set_voltage(voltage);
	pico_turbo_delay_us(PICO_TURBO_SETTLE_US);

	if (clock_get_hz(clk_sys) / 1000u != config->khz) {
		/* required=false on purpose: a frequency the PLL cannot hit exactly is
		 * not an error, it is the normal case for a caller walking a ladder --
		 * required=true panics instead of landing nearby.  The achieved value is
		 * what the return value and the state report. */
		(void)set_sys_clock_khz(config->khz, false);
	}
	sys_hz = clock_get_hz(clk_sys);

	/* set_sys_clock_khz() re-points clk_peri at clk_sys itself; repeating it here
	 * keeps the library's contract independent of that detail, and recomputes the
	 * divider from the frequency that was actually reached. */
	clock_configure_undivided(
		clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sys_hz);

	return sys_hz == config->khz * 1000u;
}

pico_turbo_state_t pico_turbo_state(void)
{
	pico_turbo_state_t state;

#ifdef PICO_TURBO_ENABLED
	state = s_state;
#else
	state = (pico_turbo_state_t){ 0 };
	state.enabled = false;
	state.reached = false;
	state.sys_clk_khz = clock_get_hz(clk_sys) / 1000u;
	state.peri_clk_khz = clock_get_hz(clk_peri) / 1000u;
	state.usb_clk_khz = clock_get_hz(clk_usb) / 1000u;
	state.usb_ok = (state.usb_clk_khz == 48000u);
#ifdef PICO_FLASH_SPI_CLKDIV
	state.flash_clk_khz = state.sys_clk_khz / PICO_FLASH_SPI_CLKDIV;
#endif
#endif

	return state;
}

void pico_turbo_init(void)
{
	pico_turbo_config_t config;

	if (s_initialised) {
		return;
	}

#ifdef PICO_TURBO_AUTOTUNE
	/* The build asked for a search rather than a number. */
	pico_turbo_autotune(NULL);

	s_initialised = true;

	return;
#else
#ifdef PICO_TURBO_BASE_CLK_KHZ
	config.khz = PICO_TURBO_BASE_CLK_KHZ;
	config.vreg_sel = pico_turbo_voltage_for_khz(PICO_TURBO_BASE_CLK_KHZ);
	(void)pico_turbo_apply(&config);
	pico_turbo_publish(&config, false, 0);
#endif
	s_initialised = true;
#endif
}

