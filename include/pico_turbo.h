/*
 * pico_turbo - A lightweight overclocking library for Raspberry Pi Pico
 *
 * Usage:
 *   1. In your CMakeLists.txt, BEFORE add_subdirectory:
 *        set(PICO_TURBO_ENABLED ON)
 *        set(PICO_TURBO_SYS_CLK_KHZ 400000)
 *        add_subdirectory(libs/pico-turbo)
 *        target_link_libraries(your_app pico_turbo)
 *
 *   2. Or from the command line:
 *        cmake -DPICO_TURBO_ENABLED=ON -DPICO_TURBO_SYS_CLK_KHZ=400000 ..
 *
 *   3. In your code, call once at startup:
 *        pico_turbo_init();
 */

#ifndef PICO_TURBO_H
#define PICO_TURBO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize overclocking.
 *
 * Call this once at the very beginning of main(), before any peripheral
 * initialization. When PICO_TURBO_ENABLED is OFF, this is a no-op.
 *
 * This function:
 *  - Sets the core voltage to match the target frequency
 *  - Reconfigures the system clock (and PLLs)
 *  - Updates the peripheral clock source
 */
void pico_turbo_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PICO_TURBO_H */
