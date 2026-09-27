/* Host stub for pico/time.h: only what the library uses. */
#ifndef _PICO_TIME_H
#define _PICO_TIME_H

#include <stdint.h>

/*: Monotonic, and advanced by the host test on demand so a soak loop terminates
 *: without waiting for real time. */
uint64_t time_us_64(void);
void test_time_advance_us(uint64_t us);

/*: No-op: the library only uses it to wait, and waiting is what the host tests
 *: want to skip.  Counted, though, so a test can tell a wait happened. */
extern unsigned test_busy_wait_calls;
void busy_wait_at_least_cycles(uint32_t cycles);

#endif
