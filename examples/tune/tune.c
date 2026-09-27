/*
 * pico_turbo tune example - measure this chip and emit its tier table
 *
 * Build and flash it, open the serial console, and it will:
 *
 *   1. search for the highest frequency this particular chip runs correctly,
 *      self-checking every candidate,
 *   2. reduce that to a handful of tiers -- the fastest stable frequency at each
 *      core voltage -- and check each of them again,
 *   3. print the whole thing as a C header, between BEGIN/END markers.
 *
 * Everything between the markers is the file: capture the console output, cut it
 * out, save it as pico_turbo_config.h.  Then in your own project:
 *
 *   #include "pico_turbo_config.h"
 *
 *   int main(void) {
 *       pico_turbo_use_table(pico_turbo_tuned_configs, PICO_TURBO_TUNED_COUNT);
 *       pico_turbo_select(PICO_TURBO_TUNED_MAX);    // or SAFE, or any index
 *       ...
 *   }
 *
 * The table is per chip, not per board model: that is the point of measuring.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "pico_turbo.h"

/* ---- how hard to look ------------------------------------------------- */

/*: A trustworthy answer matters more here than a fast boot, so the search steps
 *: finely and stresses each candidate well past what a normal boot would. */
#ifndef TUNE_STEP_KHZ
#define TUNE_STEP_KHZ 5000u
#endif
#ifndef TUNE_STRESS_MS
#define TUNE_STRESS_MS 100u
#endif

/*: The frequency to start from: must be safe on any part (stock clocks). */
#ifndef TUNE_BASE_KHZ
#define TUNE_BASE_KHZ 125000u
#endif

/*: Where to stop climbing.  Keep this close to what you expect the part to do:
 *: probing far past it is how a chip ends up needing a power cycle.  The default
 *: is the ceiling the library was built with, because a step above that is not a
 *: frequency the library will apply: it would be refused, and the trace would end
 *: with a step that says nothing about the chip. */
#ifndef TUNE_MAX_KHZ
#define TUNE_MAX_KHZ PICO_TURBO_MAX_CLK_KHZ
#endif

/*: Keep the search inside a voltage window.  Zero means "whatever the library's
 *: table says", which is what a measurement of a chip you do not know yet wants;
 *: a floor is what a search that has already been shown where the chip hangs
 *: needs, so it climbs past that point with the voltage the chip does want. */
#ifndef TUNE_MIN_VREG_SEL
#define TUNE_MIN_VREG_SEL 0u
#endif
#ifndef TUNE_MAX_VREG_SEL
#define TUNE_MAX_VREG_SEL 0u
#endif

/*: How many resets a wall is worth while looking for the ceiling.  Each one buys
 *: one voltage step at the frequency the chip hung at, so a probe that wants the
 *: real ceiling wants more than the two a normal boot would spend. */
#ifndef TUNE_MAX_HANGS
#define TUNE_MAX_HANGS 0u
#endif

/*: Final acceptance run per tier: long enough that a marginal configuration
 *: fails here rather than in the field. */
#ifndef TUNE_VERIFY_MS
#define TUNE_VERIFY_MS 200u
#endif

/*: Leave the chip at the fastest verified tier instead of the safmost one.
 *: Off by default: the file is the result of this example, and a chip left at
 *: its highest tier is the harder one to reflash. */
#ifndef TUNE_LEAVE_AT_MAX
#define TUNE_LEAVE_AT_MAX 0
#endif

/* ---- the generated header is built in RAM first ----------------------- */

/*: Kept as one string so the text can also be read back over SWD while the
 *: example runs; printf() alone would leave nothing to inspect. */
static char s_header[4096];
static size_t s_header_len;

/*: Watchdog scratch registers are the only storage that survives the reset a
 *: hang causes, so the tier on trial is written there before it is applied: the
 *: boot after a reset can then name the configuration that did it instead of
 *: blaming the run as a whole.  Slots 0..3 belong to the tuner and 4 to the SDK's
 *: own reboot marker. */
#define TUNE_SCRATCH_MAGIC 0x74756e65u /* 'tune' */
#define TUNE_SCRATCH_MAGIC_IDX 5u
#define TUNE_SCRATCH_TIER_IDX 6u /* tier on trial, 1-based; 0 = none */
#define TUNE_SCRATCH_FAILED_IDX 7u /* the tier that reset the chip, 1-based */

/*: Progress markers: unlike the console, these are readable from outside the
 *: chip (a debug probe, or a flash record), so a run that never gets to print
 *: still says how far it got and what it found. */
static volatile uint32_t s_done;
static volatile uint8_t s_tier_verified[PICO_TURBO_TIERS_MAX];
static volatile uint32_t s_tier_verified_count;
static char s_tier_result[PICO_TURBO_TIERS_MAX][16];

static void emit(const char *fmt, ...)
{
	va_list ap;
	int n;

	if (s_header_len >= sizeof(s_header) - 1) {
		return;
	}

	va_start(ap, fmt);
	n = vsnprintf(s_header + s_header_len, sizeof(s_header) - s_header_len, fmt,
		      ap);
	va_end(ap);

	if (n > 0) {
		s_header_len += (size_t)n;
		if (s_header_len > sizeof(s_header) - 1) {
			s_header_len = sizeof(s_header) - 1;
		}
	}
}

static void board_id_string(char *out, size_t len)
{
	pico_unique_board_id_t id;

	pico_get_unique_board_id(&id);
	for (size_t i = 0; i < len / 3 && i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
		snprintf(out + i * 3, 4, "%02x%c", id.id[i],
		         i + 1 < PICO_UNIQUE_BOARD_ID_SIZE_BYTES ? ':' : '\0');
	}
}

int main(void)
{
	static pico_turbo_config_t tiers[PICO_TURBO_TIERS_MAX];
	pico_turbo_autotune_t policy = {
		.base_khz = TUNE_BASE_KHZ,
		.max_khz = TUNE_MAX_KHZ,
		.step_khz = TUNE_STEP_KHZ,
		.stress_ms = TUNE_STRESS_MS,
		.min_vreg_sel = TUNE_MIN_VREG_SEL,
		.max_vreg_sel = TUNE_MAX_VREG_SEL,
		.max_hangs = TUNE_MAX_HANGS,
	};
	pico_turbo_config_t config, reference_config;
	char board_id[3 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
	const pico_turbo_step_t *steps;
	uint32_t tier_count, candidate_count, n_steps, reference, left;
	uint32_t reset_tier = 0;

	/* stdio first: this example is useless without a console. */
	stdio_init_all();
	sleep_ms(3000);

	/* Did the last run end in a reset rather than in a result?  If it did, the
	 * tier that was being verified is in the scratch registers. */
	if (watchdog_hw->scratch[TUNE_SCRATCH_MAGIC_IDX] == TUNE_SCRATCH_MAGIC) {
		reset_tier = watchdog_hw->scratch[TUNE_SCRATCH_FAILED_IDX];
	}
	watchdog_hw->scratch[TUNE_SCRATCH_MAGIC_IDX] = TUNE_SCRATCH_MAGIC;
	watchdog_hw->scratch[TUNE_SCRATCH_TIER_IDX] = 0;
	watchdog_hw->scratch[TUNE_SCRATCH_FAILED_IDX] = 0;

	printf("\n\n=== pico_turbo tune ===\n");
	printf("searching %lu..%lu kHz in %lu kHz steps, %lu ms per candidate\n",
	       (unsigned long)policy.base_khz, (unsigned long)policy.max_khz,
	       (unsigned long)policy.step_khz, (unsigned long)policy.stress_ms);
	printf("voltage window: sel %u..%u%s, %lu resets allowed at a wall\n\n",
	       (unsigned)policy.min_vreg_sel, (unsigned)policy.max_vreg_sel,
	       policy.min_vreg_sel ? " (forced)" : " (library table)",
	       (unsigned long)policy.max_hangs);

	config = pico_turbo_autotune(&policy);
	n_steps = pico_turbo_trace(&steps);

	printf("settled at %lu kHz (sel %u), %lu hangs, %lu configurations tried\n",
	       (unsigned long)config.khz, (unsigned)config.vreg_sel,
	       (unsigned long)pico_turbo_state().hangs, (unsigned long)n_steps);

	tier_count = pico_turbo_tiers_from_trace(tiers, PICO_TURBO_TIERS_MAX);
	if (tier_count == 0) {
		printf("no stable configuration found -- nothing to emit\n");
		return 1;
	}

	pico_turbo_use_table(tiers, tier_count);

	/* Verify every tier again, at length, against the chip's own answers at the
	 * starting frequency: a tier that fails here must not go into the file. */
	watchdog_enable(TUNE_VERIFY_MS * 4u + 2000u, true);
	watchdog_update();

	/* The reference is taken at the configuration the search started from: that
	 * one is known good on any chip, which a tier is not.  Taking it from tier 0
	 * would walk straight back into the reset if tier 0 is the one that reset the
	 * chip last time round. */
	reference_config.khz = policy.base_khz;
	reference_config.vreg_sel = pico_turbo_voltage_for_khz(policy.base_khz);
	(void)pico_turbo_apply(&reference_config);
	reference = pico_turbo_self_test(TUNE_VERIFY_MS);

	printf("\ntier  clock      voltage   verify\n");
	for (uint32_t i = 0; i < tier_count; i++) {
		pico_turbo_state_t now;
		bool exact;
		bool applied;
		bool good;

		/* Applying a tier is the only way to verify it: the search already ran
		 * this configuration, but a longer soak here is what turns "it worked
		 * during the search" into "it works". */
		if (reset_tier == i + 1u) {
			/* Last time round, this one reset the chip: it hung or it crashed,
			 * and either way it does not belong in the file.  It is reported as
			 * a failure rather than retried, so one bad tier costs one reset. */
			s_tier_verified[i] = 0u;
			snprintf(s_tier_result[i], sizeof(s_tier_result[i]), "reset");
			printf(" %2lu   %6lu kHz  sel %2u   %s\n", (unsigned long)i,
			       (unsigned long)tiers[i].khz, (unsigned)tiers[i].vreg_sel,
			       s_tier_result[i]);
			continue;
		}

		watchdog_hw->scratch[TUNE_SCRATCH_TIER_IDX] = i + 1u;
		watchdog_update();

		exact = pico_turbo_select(i);
		now = pico_turbo_state();
		applied = now.enabled && now.requested_khz == tiers[i].khz;
		good = applied && pico_turbo_self_check(TUNE_VERIFY_MS, reference);

		watchdog_update();
		watchdog_hw->scratch[TUNE_SCRATCH_TIER_IDX] = 0;

		s_tier_verified[i] = good ? 1u : 0u;
		snprintf(s_tier_result[i], sizeof(s_tier_result[i]), "%s",
		         !applied ? "rejected" : (good ? "pass" : "FAIL"));

		if (good) {
			s_tier_verified_count++;
		}

		printf(" %2lu   %6lu kHz  sel %2u   %s\n", (unsigned long)i,
		       (unsigned long)tiers[i].khz, (unsigned)tiers[i].vreg_sel,
		       s_tier_result[i]);

		if (!applied) {
			/* Not a configuration this build may apply.  Nothing was changed, so
			 * there is nothing to undo and no point trying the next one either:
			 * the table is not what it should be. */
			printf("table entry %lu is not applicable; refusing the rest\n",
			       (unsigned long)i);
			break;
		}
	}

	/* Only the tiers that survived go into the file: a configuration that fails
	 * its own acceptance run has no business being written down as usable. */
	candidate_count = 0;
	for (uint32_t i = 0; i < tier_count; i++) {
		if (s_tier_verified[i]) {
			tiers[candidate_count++] = tiers[i];
		}
	}

	if (candidate_count == 0) {
		printf("no tier passed verification -- nothing to emit\n");
		/* Nothing in the table can be trusted, so leave the chip at the
		 * configuration the search started from rather than at the frequency it
		 * happened to end on. */
		(void)pico_turbo_apply(&reference_config);
		s_done = 1;
		for (;;) {
			tight_loop_contents();
		}
	}

	pico_turbo_use_table(tiers, candidate_count);
	tier_count = candidate_count;

	board_id_string(board_id, sizeof(board_id));

	emit("/*\n");
	emit(" * pico_turbo: the stable configurations measured on this chip.\n");
	emit(" *\n");
	emit(" * Generated by examples/tune from a runtime search that self-checked every\n");
	emit(" * candidate and then verified each tier again at length.  Each entry is the\n");
	emit(" * highest frequency this particular chip was found to run correctly at that\n");
	emit(" * core voltage.  It describes the chip it was measured on, not the board\n");
	emit(" * model.\n");
	emit(" *\n");
	emit(" * What \"verified\" means here: a %lu ms soak of a fixed workload that\n",
	     (unsigned long)TUNE_VERIFY_MS);
	emit(" * checks the answers and reads a region of the image back through XIP.  It is\n");
	emit(" * a screen, not a guarantee: on the chip this was written for, the search\n");
	emit(" * accepted a tier that a twelve second CoreMark run could not start at, while\n");
	emit(" * the tier below it passed three runs of the same.  Treat the top tier as the\n");
	emit(" * candidate, and give it a long, mixed workload before shipping it.\n");
	emit(" *\n");
	emit(" *   pico_turbo_use_table(pico_turbo_tuned_configs, PICO_TURBO_TUNED_COUNT);\n");
	emit(" *   pico_turbo_select(PICO_TURBO_TUNED_MAX);   // or SAFE, or any index\n");
	emit(" *\n");
	emit(" * The build this is used in must allow the frequencies in it:\n");
	emit(" * PICO_TURBO_MAX_CLK_KHZ has to reach %lu000.\n",
	     (unsigned long)(tiers[tier_count - 1].khz / 1000u));
	emit(" *\n");
	emit(" * board id : %s\n", board_id);
	emit(" * measured : %s %s\n", __DATE__, __TIME__);
	emit(" * tiers    : %lu\n", (unsigned long)tier_count);
	emit(" */\n");
	emit("#ifndef PICO_TURBO_CONFIG_H\n");
	emit("#define PICO_TURBO_CONFIG_H\n\n");
	emit("#include \"pico_turbo.h\"\n\n");
	emit("#define PICO_TURBO_TUNED_BOARD_ID \"%s\"\n", board_id);
	emit("#define PICO_TURBO_TUNED_COUNT %luu\n", (unsigned long)tier_count);
	emit("#define PICO_TURBO_TUNED_SAFE  0u  /* slowest, lowest voltage */\n");
	emit("#define PICO_TURBO_TUNED_MAX   %luu  /* fastest this chip does */\n\n",
	     (unsigned long)(tier_count - 1));
	emit("/* khz = clk_sys, vreg_sel = VREG_VOLTAGE_* register encoding */\n");
	emit("static const pico_turbo_config_t "
	     "pico_turbo_tuned_configs[PICO_TURBO_TUNED_COUNT] = {\n");
	for (uint32_t i = 0; i < tier_count; i++) {
		emit("\t{ %luu, %uu }, /* %lu MHz @ sel %u */\n",
		     (unsigned long)tiers[i].khz, (unsigned)tiers[i].vreg_sel,
		     (unsigned long)(tiers[i].khz / 1000u),
		     (unsigned)tiers[i].vreg_sel);
	}
	emit("};\n\n");
	emit("#endif /* PICO_TURBO_CONFIG_H */\n");

	printf("\n---8<--- BEGIN pico_turbo_config.h ---8<---\n");
	printf("%s", s_header);
	printf("---8<--- END pico_turbo_config.h ---8<---\n\n");

	/* What is left running: the lowest tier by default -- the lowest *verified*
	 * one, because the table was compacted to the ones that passed.  A chip left
	 * at its fastest tier is the one that has to be power cycled when the next
	 * flash goes wrong, and this example has nothing more to prove by staying
	 * there. */
#if TUNE_LEAVE_AT_MAX
	left = tier_count - 1u;
#else
	left = 0u;
#endif
	(void)pico_turbo_select(left);
	printf("left running at %lu kHz, sel %u (%lu of %lu tiers verified)\n",
	       (unsigned long)pico_turbo_tier(left).khz,
	       (unsigned)pico_turbo_state().vreg_sel,
	       (unsigned long)s_tier_verified_count, (unsigned long)tier_count);
	printf("(reference taken at %lu kHz, sel %u)\n",
	       (unsigned long)reference_config.khz,
	       (unsigned)reference_config.vreg_sel);

	s_done = 1;

	/* Left armed on purpose: if this run ends because the chip hung, the
	 * watchdog resets it and the next boot knows which tier to blame.  Nothing
	 * else here is doing anything, so feeding it costs nothing. */
	for (;;) {
		watchdog_update();
		tight_loop_contents();
	}
}
