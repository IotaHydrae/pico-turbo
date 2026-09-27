/*
 * pico_turbo - the self-checking stress test
 *
 * Overclocking does not usually crash a chip, it makes it compute wrong answers:
 * a marginal core fails in the divider, in the flash timing, or in the SRAM path
 * long before it stops running.  So the test here is not "does it survive", it is
 * "does it still produce the same answers" -- a fixed workload whose result is a
 * function of the work alone, hashed together with a region of the image read back
 * through XIP.
 *
 * The same hash can be taken at a configuration known to be good and compared
 * later, which is how the search in pico_turbo_tune.c decides whether a candidate
 * is stable.
 */

#include "pico_turbo.h"

#include "pico.h"
#include "pico/time.h"

/* Provided by the SDK's linker script: the image that is being executed from. */
extern char __flash_binary_start[], __flash_binary_end[];

/*: How much of the image to read back.  Enough to cross several cache lines and
 *: to make a marginal flash divider show up, small enough to stay cheap. */
#define TURBO_FLASH_CHECK_BYTES 2048u

/*: Work per hash round.  Fixed, never scaled by time: the hash has to be the same
 *: at 125 MHz and at 400 MHz, otherwise there is nothing to compare against. */
#define TURBO_WORKLOAD_ROUNDS 512u

static uint32_t turbo_flash_hash(void)
{
	const uint8_t *p = (const uint8_t *)__flash_binary_start;
	size_t len = (size_t)(__flash_binary_end - __flash_binary_start);
	uint32_t h = 2166136261u;

	if (len > TURBO_FLASH_CHECK_BYTES) {
		len = TURBO_FLASH_CHECK_BYTES;
	}

	for (size_t i = 0; i < len; i++) {
		h = (h ^ p[i]) * 16777619u;
	}

	return h;
}

/*
 * A mixed integer workload: xorshift arithmetic, divisions and remainders with
 * runtime divisors (so the divider hardware, or its software fallback, is really
 * exercised), a little SRAM traffic, and a volatile sink at the end so that no
 * part of it can be optimised away.
 */
static uint32_t turbo_workload(uint32_t rounds)
{
	volatile uint32_t sink = 0;
	uint32_t a = 0x12345678u;
	uint32_t b = 0x9e3779b9u;
	uint32_t h = 0;

	for (uint32_t r = 0; r < rounds; r++) {
		a ^= a << 13;
		a ^= a >> 17;
		a ^= a << 5;
		b += a;
		b ^= b >> 11;
		h += (a / ((b & 0xffffu) + 1u)) + (b % ((a & 0xffu) + 1u));
		sink = h;
	}

	{
		uint32_t buf[16];

		for (uint32_t i = 0; i < 16; i++) {
			buf[i] = h ^ (i * 2654435761u);
		}
		for (uint32_t i = 0; i < 16; i++) {
			sink ^= buf[i] ^ buf[(i + 7u) & 15u];
		}
	}

	return h ^ sink;
}

uint32_t pico_turbo_self_test(uint32_t ms)
{
	uint32_t expected = turbo_workload(TURBO_WORKLOAD_ROUNDS) ^
			    turbo_flash_hash();
	uint64_t deadline = time_us_64() + (uint64_t)ms * 1000u;
	uint32_t soak = 0;
	bool have_soak = false;

	/* The soak exists for the errors that only show up after a while: voltage
	 * sag, a warm flash, a core that is fine for one round and not for the next.
	 * Sameness against the reference is the whole test, so a mismatch in here has
	 * to come back as something no reference can equal. */
	while (time_us_64() < deadline) {
		uint32_t got = turbo_workload(TURBO_WORKLOAD_ROUNDS);

		if (!have_soak) {
			soak = got;
			have_soak = true;
		} else if (got != soak) {
			return PICO_TURBO_SELF_TEST_BAD;
		}
	}

	return expected;
}

bool pico_turbo_self_check(uint32_t ms, uint32_t reference)
{
	return pico_turbo_self_test(ms) == reference;
}
