/*
 * pico_turbo benchmark example
 *
 * Runs two CPU-bound benchmarks and reports elapsed time over USB serial.
 * Flash the overclocked and stock binaries to the same Pico and compare
 * the output — the score directly reflects the clock-speed gain.
 *
 * Wiring: nothing required besides the Pico's USB port.
 * Open a serial terminal (115200 baud) after flashing.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <math.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "pico_turbo.h"

#if PICO_RP2350
#include "hardware/structs/qmi.h"
#endif

/* ================================================================== */
/*  Benchmark 1 — Prime sieve  (integer / bit-twiddling)               */
/* ================================================================== */

#define SIEVE_LIMIT 1000000u
#define BITS_PER_WORD 32u
#define SIEVE_WORDS ((SIEVE_LIMIT + 1u + BITS_PER_WORD - 1u) / BITS_PER_WORD)

static uint32_t sieve_bits[SIEVE_WORDS];

static uint32_t run_prime_sieve(void)
{
	/* Initialise — all numbers are “candidate primes” */
	for (uint32_t i = 0; i < SIEVE_WORDS; i++) {
		sieve_bits[i] = 0xFFFFFFFFu;
	}

	/* 0 and 1 are not prime */
	sieve_bits[0] &= ~0x3u;

	uint32_t count = 0;
	for (uint32_t i = 2; i <= SIEVE_LIMIT; i++) {
		uint32_t word = i / BITS_PER_WORD;
		uint32_t bit = 1u << (i % BITS_PER_WORD);

		if (sieve_bits[word] & bit) {
			count++;

			/* Cross off multiples */
			for (uint32_t j = i * 2; j <= SIEVE_LIMIT; j += i) {
				sieve_bits[j / BITS_PER_WORD] &=
					~(1u << (j % BITS_PER_WORD));
			}
		}
	}
	return count;
}

/* ================================================================== */
/*  Benchmark 2 — Mandelbrot escape-time  (single-precision float)     */
/* ================================================================== */

#define MANDEL_WIDTH 80u
#define MANDEL_HEIGHT 50u
#define MANDEL_MAX_ITER 256u

static uint32_t run_mandelbrot(void)
{
	uint32_t total_iters = 0;

	for (uint32_t py = 0; py < MANDEL_HEIGHT; py++) {
		float cy =
			-1.5f + 3.0f * (float)py / (float)(MANDEL_HEIGHT - 1);

		for (uint32_t px = 0; px < MANDEL_WIDTH; px++) {
			float cx = -2.0f +
				   3.0f * (float)px / (float)(MANDEL_WIDTH - 1);

			float x = 0.0f, y = 0.0f;
			uint32_t iter;
			for (iter = 0;
			     iter < MANDEL_MAX_ITER && (x * x + y * y) <= 4.0f;
			     iter++) {
				float xt = x * x - y * y + cx;
				y = 2.0f * x * y + cy;
				x = xt;
			}
			total_iters += iter;
		}
	}
	return total_iters;
}

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

static uint64_t time_benchmark(uint32_t (*fn)(void), uint32_t *result)
{
	absolute_time_t t0 = get_absolute_time();
	*result = fn();
	absolute_time_t t1 = get_absolute_time();
	return absolute_time_diff_us(t0, t1);
}
/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main(void)
{
	/* --- 1. Apply overclock (no-op when disabled) ------------------ */
	pico_turbo_init();

	/* --- 2. Bring up USB serial ----------------------------------- */
	stdio_init_all();
	/* Allow time for the host to enumerate the USB CDC device. */
	sleep_ms(3000);

	/* --- 3. Header ------------------------------------------------ */
	printf("\n\n\n");
	printf("============================================\n");
	printf("  pico_turbo Benchmark\n");
	printf("============================================\n\n");

	/* --- 4. Clock info -------------------------------------------- */
#ifdef PICO_TURBO_ENABLED
	printf("Overclocking  : ENABLED\n");
	printf("Target clock  : %lu kHz  (%lu MHz)\n",
	       (uint32_t)PICO_TURBO_SYS_CLK_KHZ,
	       (uint32_t)(PICO_TURBO_SYS_CLK_KHZ / 1000u));
#else
	printf("Overclocking  : DISABLED (stock)\n");
#endif

	uint32_t actual_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
	printf("Actual clock  : %lu kHz  (%lu MHz)\n", actual_khz,
	       actual_khz / 1000u);

	uint32_t flash_khz = actual_khz / PICO_FLASH_SPI_CLKDIV;
	printf("Flash clock   : %lu kHz  (%lu MHz, DIV %d)\n\n",
	       flash_khz, flash_khz / 1000u, PICO_FLASH_SPI_CLKDIV);

	/* --- 5. QMI debug registers (RP2350 only, keep for diagnostics) -- */
#if PICO_RP2350
	printf("QMI DIRECT_CSR: %08lx\n", qmi_hw->direct_csr);
	printf("QMI M0_TIMING  : %08lx\n\n", qmi_hw->m[0].timing);
#endif

	/* --- 6. Run benchmarks ---------------------------------------- */
	uint32_t prime_count;
	uint64_t prime_us = time_benchmark(run_prime_sieve, &prime_count);

	uint32_t mandel_iters;
	uint64_t mandel_us = time_benchmark(run_mandelbrot, &mandel_iters);

	/* --- 6. Results ----------------------------------------------- */
	printf("-- Prime Sieve  (limit = %u) -------------------\n",
	       SIEVE_LIMIT);
	printf("  Primes found : %lu\n", prime_count);
	printf("  Elapsed time : %llu us\n", prime_us);
	printf("  Score        : %lu\n\n",
	       (uint32_t)((uint64_t)SIEVE_LIMIT * 1000000u / prime_us));

	printf("-- Mandelbrot  (%ux%u, max %u iter) ----------\n", MANDEL_WIDTH,
	       MANDEL_HEIGHT, MANDEL_MAX_ITER);
	printf("  Total iters  : %lu\n", mandel_iters);
	printf("  Elapsed time : %llu us\n", mandel_us);
	printf("  Score        : %lu\n\n",
	       (uint32_t)((uint64_t)mandel_iters * 1000000u / mandel_us));

	/* Combined score (geometric mean × 100, higher = better) */
	uint32_t combined = (uint32_t)(sqrtf((float)((uint64_t)SIEVE_LIMIT *
						     1000000u / prime_us) *
					     (float)((uint64_t)mandel_iters *
						     1000000u / mandel_us)) *
				       100.0f);
	printf("-- Combined Score -----------------------------\n");
	printf("  %lu\n\n", combined);

	printf("============================================\n");
	printf("  Done.  Flash the other .uf2 and compare!\n");
	printf("============================================\n\n");

	/* --- 7. Idle -------------------------------------------------- */
	while (1) {
		sleep_ms(1000);
	}

	return 0;
}
