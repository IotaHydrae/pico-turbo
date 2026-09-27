/* Host stub for hardware/clocks.h. */
#ifndef _HARDWARE_CLOCKS_H
#define _HARDWARE_CLOCKS_H

#include <stdbool.h>
#include <stdint.h>

typedef unsigned int uint;

enum clock_index {
	clk_gpout0,
	clk_gpout1,
	clk_gpout2,
	clk_gpout3,
	clk_ref,
	clk_sys,
	clk_peri,
	clk_usb,
	clk_adc,
	clk_rtc,
	CLK_COUNT
};

/** The SDK's own settling delay after a regulator change, in microseconds. */
#define SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST_DELAY_US 1000u

#define CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS 0x0u

uint32_t clock_get_hz(enum clock_index clk_index);
void test_clock_set_hz(enum clock_index clk_index, uint32_t hz);

bool check_sys_clock_khz(uint32_t freq_khz, uint *vco_out, uint *postdiv1_out,
                         uint *postdiv2_out);
bool set_sys_clock_khz(uint32_t freq_khz, bool required);
bool clock_configure_undivided(enum clock_index clk_index, uint32_t src,
                              uint32_t auxsrc, uint32_t src_freq);

/*: Which frequencies the fake PLL can hit: anything at or below this. */
void test_pll_set_max_khz(uint32_t khz);

#endif
