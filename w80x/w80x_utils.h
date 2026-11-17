/*
 * w80x_utils.h - Measuring time in CPU cycles. Header-only.
 *
 *   uint32_t t0 = CPU_TICKS;
 *   do_something();
 *   uint32_t cyc = cpu_cycles_since(t0);
 *   printf("%lu cycles, %lu us\n", (unsigned long)cyc,
 *          (unsigned long)cpu_cycles_to_us(cyc));
 *
 * CPU_TICKS (w80x.h) is CORET, a 24-bit counter at the CPU clock, so one
 * measurement can cover at most 2^24 - 1 cycles: 69.9 ms at 240 MHz, 210 ms
 * at 80 MHz. Longer intervals wrap and come out too short.
 *
 * For longer intervals, add up steps while polling at least that often:
 *
 *   uint32_t last = CPU_TICKS, total = 0;
 *   while (total < cpu_ms_to_cycles(500)) {
 *       total += cpu_cycles_step(&last);
 *   }
 *
 * The conversions use SystemCoreClock (kept current by SystemInit() and
 * clock_set_cpu_div()), so convert before changing the CPU clock. They
 * round down. Everything is forced inline, so a measurement costs only the
 * CORET read and a subtract.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_UTILS_H
#define W80X_UTILS_H

#include <stdint.h>
#include "w80x.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CPU cycles elapsed since start, a CPU_TICKS reading. */
W80X_INLINE uint32_t cpu_cycles_since(uint32_t start)
{
    return (start - CPU_TICKS) & CORET_MAX;     /* down counter */
}

/* CPU cycles since *last, a CPU_TICKS reading, and moves *last to now.
 * CORET is read once, so no cycles are lost between steps. Call it at least
 * once per CORET wrap (see above) to keep a running total. */
W80X_INLINE uint32_t cpu_cycles_step(uint32_t *last)
{
    uint32_t now   = CPU_TICKS;
    uint32_t delta = (*last - now) & CORET_MAX;  /* down counter */

    *last = now;
    return delta;
}

/* CPU cycles to microseconds / milliseconds at the current CPU clock. */
W80X_INLINE uint32_t cpu_cycles_to_us(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000000u);
}

W80X_INLINE uint32_t cpu_cycles_to_ms(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000u);
}

/* Milliseconds to CPU cycles at the current CPU clock. The result is 32 bits:
 * up to 17.8 s at 240 MHz, 53.6 s at 80 MHz. */
W80X_INLINE uint32_t cpu_ms_to_cycles(uint32_t ms)
{
    return ms * (SystemCoreClock / 1000u);
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_UTILS_H */
