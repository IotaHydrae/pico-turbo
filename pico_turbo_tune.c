/*
 * pico_turbo - the runtime frequency/voltage search
 *
 * Silicon is not uniform: how far a part clocks depends on the die, the board, the
 * flash and the temperature.  This walks the two ladders at boot -- frequency
 * upward from PICO_TURBO_SYS_CLK_KHZ, voltage upward from the table's
 * recommendation -- self-checking every candidate, and leaves the chip at the
 * highest one that passed.
 *
 * Two things make that safe to do unattended:
 *
 *  - The flash divider is *not* touched at runtime.  It is built for the highest
 *    frequency the search may reach (see the CMake side), so it is never the
 *    reason a candidate fails; what a wrong divider would do is make the image
 *    read back wrong, and the self-check hashes a region of the image for exactly
 *    that reason.
 *  - A candidate that hangs instead of failing is caught by the watchdog.  The
 *    candidate on trial is written into the watchdog scratch registers first, so
 *    the reset does not lose it: the next boot records it as hung and comes back
 *    at the best configuration that had already passed.
 *
 * The best configuration is kept in the scratch registers as well, so a warm
 * reset skips the search instead of repeating it; a power cycle starts over.
 */

#include "pico_turbo.h"

#include <stddef.h>

#ifdef PICO_TURBO_AUTOTUNE

#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

#include "pico_turbo_internal.h"

/* watchdog_caused_reboot()/watchdog_enable_caused_reboot() keep their own marker
 * in scratch[4], so the search stays in scratch[0..3]. */
#define TURBO_SCRATCH_MAGIC 0x7074756eu /* 'ptun' */
#define TURBO_SCRATCH_MAGIC_IDX 0u
#define TURBO_SCRATCH_FLIGHT_IDX \
	1u /* (khz << 8) | sel of the candidate being tried */
#define TURBO_SCRATCH_BEST_IDX 2u /* (khz << 8) | sel of the best that passed */
#define TURBO_SCRATCH_HANGS_IDX \
	3u /* watchdog resets this power-up has caused */

/*: Frequency step when the caller does not say.  The step only has to be fine
 *: enough to land on, or just past, every frequency the PLL can produce: a
 *: candidate the PLL cannot hit exactly is skipped, not rounded (see
 *: pico_turbo_apply()), so a coarse step wastes the frequencies in between. */
#ifndef PICO_TURBO_STEP_KHZ
#define PICO_TURBO_STEP_KHZ 5000u
#endif

/*: How long each candidate is stressed when the caller does not say.  Longer
 *: means fewer false positives on a marginal chip and a slower boot. */
#ifndef PICO_TURBO_STRESS_MS
#define PICO_TURBO_STRESS_MS 30u
#endif

/*: Give up on a chip that keeps hanging: after this many watchdog resets in one
 *: power-up the search stops climbing and stays at the base configuration. */
#ifndef PICO_TURBO_MAX_HANGS
#define PICO_TURBO_MAX_HANGS 2u
#endif

#define TURBO_PACK(khz, sel) (((uint32_t)(khz) << 8) | ((sel) & 0xffu))

static pico_turbo_step_t s_trace[PICO_TURBO_TRACE_STEPS];
static uint32_t s_trace_len;

void pico_turbo_trace_add(uint32_t khz, uint16_t vreg_sel, uint8_t result)
{
	if (s_trace_len < PICO_TURBO_TRACE_STEPS) {
		s_trace[s_trace_len].khz = khz;
		s_trace[s_trace_len].vreg_sel = vreg_sel;
		s_trace[s_trace_len].result = result;
		s_trace_len++;
	}
}

uint32_t pico_turbo_trace(const pico_turbo_step_t **steps)
{
	*steps = s_trace;

	return s_trace_len;
}

void pico_turbo_trace_reset(void)
{
	s_trace_len = 0;
}

static pico_turbo_autotune_t
policy_defaults(const pico_turbo_autotune_t *policy)
{
	pico_turbo_autotune_t p = {
		.base_khz = PICO_TURBO_BASE_CLK_KHZ,
		.max_khz = PICO_TURBO_MAX_CLK_KHZ,
		.step_khz = PICO_TURBO_STEP_KHZ,
		.stress_ms = PICO_TURBO_STRESS_MS,
		.min_vreg_sel = 0,
		.max_vreg_sel = PICO_TURBO_MAX_VREG_SEL,
	};

	if (policy) {
		if (policy->base_khz) {
			p.base_khz = policy->base_khz;
		}
		if (policy->max_khz) {
			p.max_khz = policy->max_khz;
		}
		if (policy->step_khz) {
			p.step_khz = policy->step_khz;
		}
		if (policy->stress_ms) {
			p.stress_ms = policy->stress_ms;
		}
		if (policy->min_vreg_sel) {
			p.min_vreg_sel = policy->min_vreg_sel;
		}
		if (policy->max_vreg_sel) {
			p.max_vreg_sel = policy->max_vreg_sel;
		}
	}

	if (p.max_khz < p.base_khz) {
		p.max_khz = p.base_khz;
	}
	if (p.max_vreg_sel < p.min_vreg_sel) {
		p.max_vreg_sel = p.min_vreg_sel;
	}

	return p;
}

/*: How long a candidate gets before the watchdog calls it hung: the stress test
 *: plus room for the settle delay and the fixed part of the workload. */
static uint32_t hang_timeout_ms(const pico_turbo_autotune_t *p)
{
	return p->stress_ms + 200u;
}

static uint16_t voltage_floor(const pico_turbo_autotune_t *p, uint32_t khz,
			      uint16_t never_below)
{
	uint16_t sel = pico_turbo_voltage_for_khz(khz);

	if (sel < p->min_vreg_sel) {
		sel = p->min_vreg_sel;
	}
	if (sel < never_below) {
		sel = never_below;
	}
	if (sel > p->max_vreg_sel) {
		sel = p->max_vreg_sel;
	}

	return sel;
}

uint32_t pico_turbo_tiers_from_trace(pico_turbo_config_t *out, uint32_t max)
{
	uint32_t written = 0;
	uint16_t last_sel = 0;
	bool have_sel = false;

	if (!out || max == 0) {
		return 0;
	}

	/* The trace is in climb order, so the last stable entry at a given voltage is
	 * the fastest the chip did at that voltage: emit an entry whenever the voltage
	 * changes, which is the moment the previous one becomes final. */
	for (uint32_t i = 0; i < s_trace_len; i++) {
		pico_turbo_config_t config;

		if (s_trace[i].result != PICO_TURBO_STEP_STABLE) {
			continue;
		}

		if (have_sel && s_trace[i].vreg_sel == last_sel) {
			/* same voltage, higher frequency: replace the entry */
			if (written == 0) {
				continue;
			}
			out[written - 1].khz = s_trace[i].khz;
			continue;
		}

		if (written == max) {
			break;
		}

		config.khz = s_trace[i].khz;
		config.vreg_sel = s_trace[i].vreg_sel;
		out[written++] = config;
		last_sel = s_trace[i].vreg_sel;
		have_sel = true;
	}

	return written;
}

pico_turbo_config_t pico_turbo_autotune(const pico_turbo_autotune_t *policy)
{
	pico_turbo_autotune_t p = policy_defaults(policy);
	pico_turbo_config_t config;
	uint32_t stored_best = 0;
	uint32_t hangs = 0;
	uint32_t inflight = 0;
	uint32_t golden;

	if (watchdog_hw->scratch[TURBO_SCRATCH_MAGIC_IDX] ==
	    TURBO_SCRATCH_MAGIC) {
		stored_best = watchdog_hw->scratch[TURBO_SCRATCH_BEST_IDX];
		hangs = watchdog_hw->scratch[TURBO_SCRATCH_HANGS_IDX];
		inflight = watchdog_hw->scratch[TURBO_SCRATCH_FLIGHT_IDX];
	}
	watchdog_hw->scratch[TURBO_SCRATCH_MAGIC_IDX] = TURBO_SCRATCH_MAGIC;
	watchdog_hw->scratch[TURBO_SCRATCH_FLIGHT_IDX] = 0;
	pico_turbo_trace_reset();

	/* A candidate was on trial when the chip went away: it hung. */
	if (inflight != 0) {
		pico_turbo_trace_add(inflight >> 8,
				     (uint16_t)(inflight & 0xffu),
				     PICO_TURBO_STEP_HANG);
		hangs++;
		watchdog_hw->scratch[TURBO_SCRATCH_HANGS_IDX] = hangs;
	}

	/* Come back to a configuration the chip is known to run *before* acting on
	 * anything a previous run left behind.  If the last attempt took the chip
	 * down hard, the boot that follows must not depend on that state being
	 * readable, let alone good: the base frequency at its table voltage is the
	 * one point the caller promised would work. */
	{
		pico_turbo_config_t safe = {
			.khz = p.base_khz,
			.vreg_sel = pico_turbo_voltage_for_khz(p.base_khz),
		};

		(void)pico_turbo_apply(&safe);
	}

	/* Start from what an earlier run proved, unless the caller asked for its own
	 * search (in which case it has its own idea of where to begin). */
	if (policy == NULL && (stored_best >> 8) >= p.base_khz) {
		config.khz = stored_best >> 8;
		config.vreg_sel = (uint16_t)(stored_best & 0xffu);
	} else {
		config.khz = p.base_khz;
		config.vreg_sel = pico_turbo_voltage_for_khz(p.base_khz);
	}
	config.vreg_sel = voltage_floor(&p, config.khz, config.vreg_sel);

	(void)pico_turbo_apply(&config);

	/* The chip keeps hanging: stop climbing, stay at the base. */
	if (hangs > PICO_TURBO_MAX_HANGS) {
		config.khz = p.base_khz;
		config.vreg_sel = pico_turbo_voltage_for_khz(p.base_khz);
		(void)pico_turbo_apply(&config);
		pico_turbo_publish(&config, false, hangs);

		return config;
	}

	/* A hang ends the search for this power-up, not just for this boot.  The
	 * configuration applied above is the best one that had passed, and
	 * climbing past it again would mean hitting the same wall again: once the
	 * chip has had to be reset, every later boot stops here too (until the
	 * chip is power-cycled, which is also how you would retry it after fixing
	 * the cooling).  A hang says more than a checksum mismatch. */
	if (hangs > 0) {
		pico_turbo_publish(&config, true, hangs);

		return config;
	}

	/* Arm the watchdog for the search as a whole and feed it between candidates:
	 * everything below -- applying a configuration, hashing the image, stressing the
	 * core -- runs before the chip has proven it can do any of it, and a hang in
	 * there has to end in a reset rather than a dead board.  The candidate that was
	 * on trial is in the scratch registers, so the next boot resumes. */
	watchdog_enable(p.stress_ms * 3u + 2000u, true);
	watchdog_update();

	/* The reference every candidate is compared against, taken at a configuration
	 * that is known to be good. */
	golden = pico_turbo_self_test(p.stress_ms);
	watchdog_update();

	for (uint32_t khz = config.khz + p.step_khz; khz <= p.max_khz;
	     khz += p.step_khz) {
		uint vco, postdiv1, postdiv2;
		uint16_t sel;
		bool passed = false;

		/* The PLL produces a discrete set of frequencies and the SDK refuses to
		 * round to a neighbour: asking for one that cannot be hit exactly leaves
		 * the clock where it was.  Check first, so a step that is not a real
		 * PLL point costs nothing and is recorded as skipped. */
		if (!check_sys_clock_khz(khz, &vco, &postdiv1, &postdiv2)) {
			pico_turbo_trace_add(khz, voltage_floor(&p, khz, 0),
					     PICO_TURBO_STEP_SKIPPED);
			continue;
		}

		sel = voltage_floor(&p, khz, config.vreg_sel);

		/* The voltage only ever goes up while climbing: a frequency that needed
		 * more keeps it, which keeps the search monotone. */
		for (; sel <= p.max_vreg_sel; sel++) {
			pico_turbo_config_t trial = { .khz = khz,
						      .vreg_sel = sel };
			uint32_t achieved;

			/* Write the candidate down before touching anything: if the apply or
			 * the stress hangs, the reset leaves this behind for the next boot. */
			watchdog_hw->scratch[TURBO_SCRATCH_FLIGHT_IDX] =
				TURBO_PACK(khz, sel);
			watchdog_enable(hang_timeout_ms(&p), true);

			(void)pico_turbo_apply(&trial);
			achieved = clock_get_hz(clk_sys) / 1000u;
			trial.khz =
				achieved; /* judge what was reached, not what was asked */

			/* Applied, but the clock did not move: nothing to stress. */
			if (achieved <= config.khz) {
				pico_turbo_trace_add(achieved, sel,
						     PICO_TURBO_STEP_SKIPPED);
				break;
			}

			watchdog_enable(hang_timeout_ms(&p), true);

			if (pico_turbo_self_check(p.stress_ms, golden)) {
				watchdog_update();
				watchdog_hw->scratch[TURBO_SCRATCH_FLIGHT_IDX] =
					0;
				config = trial;
				watchdog_hw->scratch[TURBO_SCRATCH_BEST_IDX] =
					TURBO_PACK(config.khz, config.vreg_sel);
				pico_turbo_trace_add(achieved, sel,
						     PICO_TURBO_STEP_STABLE);
				passed = true;
				break;
			}

			watchdog_update();
			watchdog_update();
			pico_turbo_trace_add(achieved, sel,
					     PICO_TURBO_STEP_FAILED);
		}

		if (!passed) {
			/* Nothing worked at this frequency; assume higher ones are worse. */
			break;
		}
	}

	watchdog_disable();
	watchdog_hw->scratch[TURBO_SCRATCH_FLIGHT_IDX] = 0;
	/* A search that ran to the end clears the hang history: the chip has just
	 * demonstrated that it can do this much. */
	watchdog_hw->scratch[TURBO_SCRATCH_HANGS_IDX] = 0;

	(void)pico_turbo_apply(&config);
	pico_turbo_publish(&config, true, 0);

	return config;
}

#else /* !PICO_TURBO_AUTOTUNE */

void pico_turbo_trace_reset(void)
{
}

/*: A build that does not search has no trace to report, but the call has to
 *: exist: the API is the same in every build, and an application that prints the
 *: trace of a fixed-frequency build is asking a fair question. */
uint32_t pico_turbo_trace(const pico_turbo_step_t **steps)
{
	if (steps) {
		*steps = NULL;
	}

	return 0u;
}

uint32_t pico_turbo_tiers_from_trace(pico_turbo_config_t *out, uint32_t max)
{
	/* Nothing was searched, so there is no trace to reduce. */
	(void)out;
	(void)max;

	return 0u;
}

pico_turbo_config_t pico_turbo_autotune(const pico_turbo_autotune_t *policy)
{
	pico_turbo_state_t state = pico_turbo_state();
	pico_turbo_config_t config = { .khz = state.requested_khz,
				       .vreg_sel = state.vreg_sel };

	(void)policy;

	return config;
}

#endif /* PICO_TURBO_AUTOTUNE */
