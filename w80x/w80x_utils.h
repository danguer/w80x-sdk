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

/* CPU cycles to microseconds / milliseconds at the current CPU clock. */
W80X_INLINE uint32_t cpu_cycles_to_us(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000000u);
}

W80X_INLINE uint32_t cpu_cycles_to_ms(uint32_t cycles)
{
    return cycles / (SystemCoreClock / 1000u);
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_UTILS_H */
