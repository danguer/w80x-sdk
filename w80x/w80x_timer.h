/*
 * w80x_timer.h - Timers TIMER0..TIMER5 (manual chapter 19). Header-only.
 *
 * Six independent 32-bit timers counting a 1 us tick (APB clock / TMR_CONFIG).
 * Each one runs in us or ms, once or repeatedly, with an optional interrupt.
 * All six share one interrupt, TIMER_IRQn.
 *
 *   timer_init();
 *   timer_start(TIMER0, 500, TIMER_MS | TIMER_PERIODIC | TIMER_IRQ);
 *   irq_enable(TIMER_IRQn);
 *   __enable_irq();
 *
 *   W80X_IRQ void TIMER_IRQHandler(void)
 *   {
 *       uint32_t fired = timer_irq_status();   // bit n = timer n
 *       timer_irq_clear(fired);
 *       if (fired & BIT(TIMER0)) led_toggle(LED1);
 *   }
 *
 * Delays: timer_delay_us() / timer_delay_ms() wait on a one-shot timer
 * (TIMER_DELAY_ID, TIMER5 by default; build with -DTIMER_DELAY_ID=TIMERn to pick
 * another). Unlike delay_us()/delay_ms() (CORET busy loops), they count
 * wall-clock time from the APB clock, go up to 2^32 us / 2^32 ms, and can be
 * interrupted for any length of time without drifting. Don't use the delay
 * timer for anything else.
 *
 * The tick comes from the APB clock. Some CPU clocks lower APB (see
 * w80x_clock.h), so call timer_init() again after clock_set_cpu_div() (the
 * delay functions do this themselves).
 *
 * TMR_CSR holds all six timers' bits, including write-1-to-clear interrupt
 * flags. Every write here masks the flags of the other timers, so changing
 * one timer never clears another's pending interrupt.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_TIMER_H
#define W80X_TIMER_H

#include "w80x.h"
#include "w80x_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TIMER_DELAY_ID
#define TIMER_DELAY_ID  TIMER5
#endif

/* timer_start() flags */
#define TIMER_US        0x0u     /* period in microseconds (default) */
#define TIMER_MS        0x1u     /* period in milliseconds           */
#define TIMER_ONESHOT   0x0u     /* run once, then stop (default)    */
#define TIMER_PERIODIC  0x2u     /* restart on every expiry          */
#define TIMER_IRQ       0x4u     /* interrupt on expiry              */

/* Read-modify-write TMR_CSR without touching other timers' flags. */
W80X_INLINE void timer__csr(uint32_t clear, uint32_t set)
{
    uint32_t psr = irq_save();
    TIMER->CSR = (TIMER->CSR & ~(TIMER_CSR_IF_ALL | clear)) | set;
    irq_restore(psr);
}

/* Prescaler for a 1 us tick from the current APB clock. */
W80X_INLINE uint32_t timer__prescale(void)
{
    uint32_t mhz = clock_apb_hz() / 1000000u;
    return ((mhz ? mhz : 1u) - 1u) & TIMER_CFG_Msk;
}

/* Clock on, out of reset, 1 us tick. Leaves running timers alone. */
W80X_INLINE void timer_init(void)
{
    CLK->GATE_EN  |= CLK_GATE_TIMER;
    CLK->RST_CTRL |= CLK_RST_TIMER;
    TIMER->CFG = timer__prescale();
}

/* ---- control ------------------------------------------------------------ */

W80X_INLINE void timer_stop(uint32_t id)
{
    /* off, no interrupt, and write 1 to its flag to clear it */
    timer__csr(TIMER_CSR_EN(id) | TIMER_CSR_IE(id), TIMER_CSR_IF(id));
}

/* (Re)start timer id: period in us or ms (>= 1), flags = TIMER_US/MS |
 * TIMER_ONESHOT/PERIODIC | TIMER_IRQ. */
W80X_INLINE void timer_start(uint32_t id, uint32_t period, uint32_t flags)
{
    timer_stop(id);
    TIMER->PRD[id] = period ? period : 1u;

    uint32_t set = TIMER_CSR_EN(id);
    if (flags & TIMER_MS) {
        set |= TIMER_CSR_MS(id);
    }
    if (!(flags & TIMER_PERIODIC)) {
        set |= TIMER_CSR_ONESHOT(id);
    }
    if (flags & TIMER_IRQ) {
        set |= TIMER_CSR_IE(id);
    }
    timer__csr(TIMER_CSR_FIELD(id), set);
}

/* New period; the timer restarts counting from zero. */
W80X_INLINE void timer_set_period(uint32_t id, uint32_t period)
{
    uint32_t keep = TIMER->CSR & TIMER_CSR_FIELD(id) & ~TIMER_CSR_IF(id);
    timer__csr(TIMER_CSR_EN(id), 0);
    TIMER->PRD[id] = period ? period : 1u;
    timer__csr(TIMER_CSR_FIELD(id), keep | TIMER_CSR_EN(id));
}

/* A one-shot timer stops itself on expiry, so this also tells "done". */
W80X_INLINE int      timer_running(uint32_t id) { return (TIMER->CSR & TIMER_CSR_EN(id)) != 0; }
W80X_INLINE uint32_t timer_period(uint32_t id)  { return TIMER->PRD[id]; }
W80X_INLINE uint32_t timer_count(uint32_t id)   { return TIMER->CNT[id]; }   /* raw TMRn_CNT */

/* ---- interrupts ----------------------------------------------------------- */

W80X_INLINE void timer_irq_enable(uint32_t id)  { timer__csr(0, TIMER_CSR_IE(id)); }
W80X_INLINE void timer_irq_disable(uint32_t id) { timer__csr(TIMER_CSR_IE(id), 0); }

/* Timers whose flag is set, as a mask: bit n = timer n. */
W80X_INLINE uint32_t timer_irq_status(void)
{
    uint32_t csr = TIMER->CSR;
    uint32_t pending = 0;
    for (uint32_t id = 0; id < TIMER_NUM; id++) {
        if (csr & TIMER_CSR_IF(id)) {
            pending |= BIT(id);
        }
    }
    return pending;
}

/* Clear flags for the timers in mask (bit n = timer n). */
W80X_INLINE void timer_irq_clear(uint32_t mask)
{
    uint32_t flags = 0;
    for (uint32_t id = 0; id < TIMER_NUM; id++) {
        if (mask & BIT(id)) {
            flags |= TIMER_CSR_IF(id);
        }
    }
    uint32_t psr = irq_save();
    TIMER->CSR = (TIMER->CSR & ~TIMER_CSR_IF_ALL) | flags;
    irq_restore(psr);
}

W80X_INLINE int timer_expired(uint32_t id) { return (TIMER->CSR & TIMER_CSR_IF(id)) != 0; }

/* ---- delays ------------------------------------------------------------- */

/* Wait for a one-shot run of the delay timer. It stops itself when the time
 * is up (manual 19.4.3); the flag check is a second way out. */
W80X_INLINE void timer__delay(uint32_t period, uint32_t unit)
{
    if (!period) {
        return;
    }
    timer_init();
    timer_start(TIMER_DELAY_ID, period, unit | TIMER_ONESHOT);
    while (timer_running(TIMER_DELAY_ID) && !timer_expired(TIMER_DELAY_ID)) {
    }
    timer_stop(TIMER_DELAY_ID);
}

W80X_INLINE void timer_delay_us(uint32_t us) { timer__delay(us, TIMER_US); }
W80X_INLINE void timer_delay_ms(uint32_t ms) { timer__delay(ms, TIMER_MS); }

#ifdef __cplusplus
}
#endif

#endif /* W80X_TIMER_H */
