# Host tests for pico-turbo: no SDK, no board.
#
#   make -C test/host        # build and run everything (C policy suite + turbocfg)
#   make -C test/host check-c
#   make -C test/host check-tools
#   make -C test/host clean
#
# Exit codes: the C suite exits 0 (pass) or 1 (fail); the Python suite does the
# same.  Neither has an INVALID_USAGE or ENVIRONMENT_ERROR surface, so those
# workspace codes are not invented here.  See docs/testing.md.

.DEFAULT_GOAL := check

CC ?= cc
PYTHON ?= python3
CFLAGS ?= -std=c11 -O1 -g -Wall -Wextra -Werror
INCLUDES := -Istub -I../../include
DEFINES := -DPICO_RP2040=1 \
	-DPICO_TURBO_ENABLED=1 \
	-DPICO_TURBO_AUTOTUNE=1 \
	-DPICO_TURBO_SYS_CLK_KHZ=125000 \
	-DPICO_TURBO_MAX_CLK_KHZ=420000 \
	-DPICO_TURBO_STEP_KHZ=5000 \
	-DPICO_TURBO_STRESS_MS=30

SOURCES := stub.c test_turbo.c \
	../../pico_turbo.c \
	../../pico_turbo_tune.c \
	../../pico_turbo_selftest.c

test_turbo: $(SOURCES) test.h $(wildcard stub/hardware/*.h) $(wildcard stub/hardware/structs/*.h) $(wildcard ../../include/*.h)
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) $(SOURCES) -o $@

.PHONY: all check check-c check-tools clean
all: check

check: check-c check-tools

check-c: test_turbo
	./test_turbo

check-tools: ../../tools/turbocfg.py test_turbocfg.py
	$(PYTHON) test_turbocfg.py

clean:
	rm -f test_turbo
