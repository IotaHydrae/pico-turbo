# Host tests for pico-turbo: no SDK, no board.
#
#   make -C test/host        # build and run
#   make -C test/host clean

.DEFAULT_GOAL := check

CC ?= cc
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

.PHONY: all check clean
all: check

check: test_turbo
	./test_turbo

clean:
	rm -f test_turbo
