/*
 * pico_turbo - Runtime overclocking implementation
 *
 * Voltage / frequency guidelines (RP2040):
 *   <= 266 MHz  →  1.10 V  (default)
 *   <= 360 MHz  →  1.20 V
 *   <= 396 MHz  →  1.25 V
 *    > 396 MHz  →  1.30 V  (max)
 *
 * Voltage / frequency guidelines (RP2350, voltage limit disabled):
 *   <= 150 MHz  →  1.10 V  (default)
 *   <= 300 MHz  →  1.20 V
 *   <= 384 MHz  →  1.30 V
 *   <= 440 MHz  →  1.40 V
 *   <= 500 MHz  →  1.50 V
 *    > 500 MHz  →  1.60 V
 *
 * NOTE for RP2350: voltages above VREG_VOLTAGE_MAX (1.30 V) require
 * vreg_disable_voltage_limit() first.  apply_voltage_rp2350() calls it
 * unconditionally so finer-grained constants (1.35 V … 3.30 V) are
 * available when needed.
 */

#include "pico_turbo.h"

#ifdef PICO_TURBO_ENABLED

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/platform.h"
#include "pico/time.h"

/*
 * We expect PICO_TURBO_SYS_CLK_KHZ to be defined by CMake at build time.
 * Convert it to MHz for the voltage lookup.
 */
#define TURBO_SYS_CLK_KHZ PICO_TURBO_SYS_CLK_KHZ
#define TURBO_SYS_CLK_MHZ (TURBO_SYS_CLK_KHZ / 1000u)

/* ------------------------------------------------------------------ */
/*  Voltage selection                                                 */
/* ------------------------------------------------------------------ */

#if defined(PICO_RP2040)

static void apply_voltage_rp2040(void)
{
	if (TURBO_SYS_CLK_MHZ > 396) {
		vreg_set_voltage(VREG_VOLTAGE_1_30);
	} else if (TURBO_SYS_CLK_MHZ > 360) {
		vreg_set_voltage(VREG_VOLTAGE_1_25);
	} else if (TURBO_SYS_CLK_MHZ > 266) {
		vreg_set_voltage(VREG_VOLTAGE_1_20);
	} else {
		vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
	}
}

#elif defined(PICO_RP2350)

static void apply_voltage_rp2350(void)
{
	/*
	 * RP2350 requires the voltage limit to be lifted before setting
	 * voltages above VREG_VOLTAGE_MAX (1.30 V).  Call it unconditionally
	 * so the extended constants (1.35 V … 3.30 V) are available.
	 */
	vreg_disable_voltage_limit();

	/*
	 * Voltage / frequency lookup for RP2350.
	 * Choose the lowest voltage that supports the target clock.
	 */
	if (TURBO_SYS_CLK_MHZ > 500) {
		vreg_set_voltage(VREG_VOLTAGE_1_60);
	} else if (TURBO_SYS_CLK_MHZ > 440) {
		vreg_set_voltage(VREG_VOLTAGE_1_50);
	} else if (TURBO_SYS_CLK_MHZ > 384) {
		vreg_set_voltage(VREG_VOLTAGE_1_40);
	} else if (TURBO_SYS_CLK_MHZ > 300) {
		vreg_set_voltage(VREG_VOLTAGE_1_30);
	} else if (TURBO_SYS_CLK_MHZ > 150) {
		vreg_set_voltage(VREG_VOLTAGE_1_20);
	} else {
		/* 150 MHz and below: default voltage (1.10 V) is sufficient. */
		vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
	}
}

#endif /* PICO_RP2040 / PICO_RP2350 */

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */

void pico_turbo_init(void)
{
#if defined(PICO_RP2040)
	apply_voltage_rp2040();
#elif defined(PICO_RP2350)
	apply_voltage_rp2350();
#else
#warning "pico_turbo: unknown platform — voltage not adjusted"
#endif

	/*
	 * Allow the voltage regulator to settle before changing clocks.
	 * The SDK specifies SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US
	 * (typically 200 µs) as the minimum settling time.  We wait for
	 * twice that duration, expressed in XOSC cycles for an accurate
	 * busy-wait that works even before the system timer is running.
	 */
	busy_wait_at_least_cycles(
		(uint32_t)((SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US * 2 *
			    (uint64_t)XOSC_HZ) /
			   1000000));

	/* Reconfigure the system clock (and PLLs) to the target speed.  */
	set_sys_clock_khz(TURBO_SYS_CLK_KHZ, true);

	/* Point the peripheral clock at the new system clock.  */
	clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
			TURBO_SYS_CLK_KHZ * 1000u, TURBO_SYS_CLK_KHZ * 1000u);
}

#else /* ! PICO_TURBO_ENABLED */

/*
 * Overclocking disabled — everything is a no-op so the call sites stay
 * clean and we don't pull in unnecessary hardware headers.
 */
void pico_turbo_init(void)
{
	/* intentionally empty */
}

#endif /* PICO_TURBO_ENABLED */
