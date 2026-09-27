/*
 * Shared with the host stubs: what the tests need to see of the fakes.
 */

#ifndef PICO_TURBO_TEST_H
#define PICO_TURBO_TEST_H

#include <stdbool.h>
#include <stdint.h>

/* ---- the call log: every hardware effect the library asked for ---- */

/** Record a call.  Kept as text so a failing assertion can print the sequence. */
void test_call_log_add(const char *fmt, ...);

/** How many calls have been recorded since the last clear. */
unsigned test_call_log_count(void);

/** The text of one call, or "" if there is no such entry. */
const char *test_call_log_get(unsigned index);

/** Index of the first entry containing `needle`, or -1. */
int test_call_log_find(const char *needle);

void test_call_log_clear(void);

#endif
