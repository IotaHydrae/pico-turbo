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
#include "pico/time.h"

#include "pico_turbo_internal.h"

/*: The lowest voltage each frequency range is known to work at.  The last entry
 *: has no upper bound and is the platform maximum.  A build that pins the voltage
 *: with PICO_TURBO_VREG_VOLTAGE does not derive it from a frequency, so it does
 *: not carry the table at all. */
#ifndef PICO_TURBO_VREG_VOLTAGE
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
#endif /* !PICO_TURBO_VREG_VOLTAGE */

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
#ifdef PICO_TURBO_FLASH_DIV
	/* What the library's own build says the flash is clocked at.  The boot stage 2 has
	 * to carry the same divider -- the CMake side patches it when it has to
	 * change it (see the comment there).  Reading a region of the image back in
	 * pico_turbo_self_test() is what proves the divider is actually safe. */
	s_state.flash_clk_khz = sys_khz / PICO_TURBO_FLASH_DIV;
#endif
	/* vreg_get_voltage() answers in the SDK's enum, which is the register
	 * encoding: VREG_VOLTAGE_1_30 is 0b1111, not 1300. */
	s_state.vreg_sel = (uint16_t)vreg_get_voltage();
	s_state.hangs = hangs;
}

/*: Is this a configuration the library is willing to apply at all?
 *
 * A table can come from anywhere -- a generated header, a flash record, an
 * application hard-coding a number -- and the regulator controls are not the
 * place to find out that it held nonsense: VREG_VOLTAGE_* values are register
 * encodings, so an out-of-range one used to land in the low bits of the voltage
 * select field and ask for a supply the core cannot run at.  Applied, that is
 * not a wrong frequency, it is a chip that has to be power cycled: it was
 * measured doing exactly that here, from a selection the example made out of a
 * table it had been handed. */
static bool config_valid(const pico_turbo_config_t *config)
{
	if (!config || config->khz == 0) {
		return false;
	}

	if (config->khz > PICO_TURBO_MAX_CLK_KHZ) {
		return false;
	}

	if (config->vreg_sel > PICO_TURBO_MAX_VREG_SEL) {
		return false;
	}

	return true;
}

static void set_clock(uint32_t khz)
{
	/* required=false on purpose: a frequency the PLL cannot hit exactly is not an
	 * error, it is the normal case for a caller walking a ladder -- required=true
	 * panics instead of landing nearby.  The achieved value is what the return
	 * value and the state report. */
	(void)set_sys_clock_khz(khz, false);

	/* set_sys_clock_khz() re-points clk_peri at clk_sys itself; repeating it here
	 * keeps the library's contract independent of that detail, and recomputes the
	 * divider from the frequency that was actually reached. */
	clock_configure_undivided(
		clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
		clock_get_hz(clk_sys));
}

bool pico_turbo_apply(const pico_turbo_config_t *config)
{
	enum vreg_voltage voltage;
	uint32_t sys_hz;
	uint32_t now_khz;
	bool lower_clock_first;

	if (!config_valid(config)) {
		return false;
	}

	voltage = (enum vreg_voltage)config->vreg_sel;
	now_khz = clock_get_hz(clk_sys) / 1000u;

	/* Which comes first depends on which way the operating point is moving, and
	 * both directions have a wrong way round that the chip punishes with a
	 * lockup rather than with a wrong clock:
	 *
	 *  - going up, the supply has to be there before the core is asked to run
	 *    faster than it can at the old one;
	 *  - going down, the core has to come off the old frequency before the
	 *    supply is cut to the new one, or it is briefly running a high frequency
	 *    at a low voltage.
	 *
	 * Only ever doing it one way round works for a search, which climbs, and
	 * fails for the first thing an application does after one: dropping from the
	 * frequency the search ended on to a lower tier. */
	lower_clock_first = config->khz < now_khz;

	/* Above the platform maximum the limit has to be lifted first (RP2350; on
	 * RP2040 the limit is hard-wired and this is a no-op). */
	if (voltage > VREG_VOLTAGE_MAX) {
		vreg_disable_voltage_limit();
	}

	if (lower_clock_first) {
		set_clock(config->khz);
	}

	vreg_set_voltage(voltage);
	pico_turbo_delay_us(PICO_TURBO_SETTLE_US);

	if (!lower_clock_first && clock_get_hz(clk_sys) / 1000u != config->khz) {
		set_clock(config->khz);
	}
	sys_hz = clock_get_hz(clk_sys);

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
#ifdef PICO_TURBO_FLASH_DIV
	state.flash_clk_khz = state.sys_clk_khz / PICO_TURBO_FLASH_DIV;
#endif
#endif

	return state;
}

/*: Tier table handed in by the application (usually from the generated header). */
static const pico_turbo_config_t *s_tiers;
static uint32_t s_tier_count;

void pico_turbo_use_table(const pico_turbo_config_t *configs, uint32_t count)
{
	s_tiers = configs;
	s_tier_count = configs ? count : 0;
}

uint32_t pico_turbo_tier_count(void)
{
	return s_tier_count;
}

pico_turbo_config_t pico_turbo_tier(uint32_t tier)
{
	pico_turbo_config_t empty = { 0 };

	if (!s_tiers || tier >= s_tier_count) {
		return empty;
	}

	return s_tiers[tier];
}

bool pico_turbo_select(uint32_t tier)
{
	pico_turbo_config_t config = pico_turbo_tier(tier);
	bool exact;

	/* A tier that is not a configuration this build may apply is refused rather
	 * than partly applied: the state is only published for a tier that was
	 * actually attempted. */
	if (!config_valid(&config)) {
		return false;
	}

	exact = pico_turbo_apply(&config);
	pico_turbo_publish(&config, false, s_state.hangs);

	return exact;
}

void pico_turbo_init(void)
{
	if (s_initialised) {
		return;
	}

#ifdef PICO_TURBO_AUTOTUNE
	/* The build asked for a search rather than a number. */
	pico_turbo_autotune(NULL);
#else
#ifdef PICO_TURBO_BASE_CLK_KHZ
	{
		pico_turbo_config_t config;

		config.khz = PICO_TURBO_BASE_CLK_KHZ;
		config.vreg_sel =
			pico_turbo_voltage_for_khz(PICO_TURBO_BASE_CLK_KHZ);
		(void)pico_turbo_apply(&config);
		pico_turbo_publish(&config, false, 0);
	}
#endif
#endif
	s_initialised = true;
}

#ifndef PICO_TURBO_AUTOTUNE
/*
 * Without a search there is nothing to report; the getter exists either way so a
 * caller does not have to care which build it links against.
 */
uint32_t pico_turbo_trace(const pico_turbo_step_t **steps)
{
	static const pico_turbo_step_t empty[1];

	*steps = empty;

	return 0;
}
#endif /* !PICO_TURBO_AUTOTUNE */
