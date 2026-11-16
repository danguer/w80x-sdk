/*
 * w80x_wdg.h - Watchdog (manual chapter 22). Header-only.
 *
 * How the hardware works (confirmed on an HLK-W801-KIT):
 *   - WD_VAL counts down from WD_LD at the APB clock (40 MHz by default) and
 *     reloads on every timeout.
 *   - Each timeout raises the watchdog interrupt flag (WDG_IRQn).
 *   - If a timeout finds the flag *still set* from the previous one, and
 *     reset is enabled, the whole chip resets.
 *   - Feeding (WD_CLR) clears the flag *and* restarts the countdown.
 *
 * So the chip resets exactly two periods after the last feed. wdg_start_ms()
 * uses a period of timeout_ms / 2:
 *
 *     last feed ----- timeout_ms/2 -----> IRQ (early warning) --- timeout_ms/2 ---> reset
 *
 *   - feed at least every timeout_ms / 2  ->  nothing happens;
 *   - feed late, but within timeout_ms    ->  early-warning IRQ only (if enabled), no reset;
 *   - stop feeding                        ->  reset timeout_ms after the last feed.
 *
 *     wdg_start_ms(2000);
 *     for (;;) { do_work(); wdg_feed(); }
 *
 * The period is computed from the APB clock when you call wdg_start_*().
 * Some CPU clocks lower APB (see w80x_clock.h), so call wdg_start_ms() again
 * after clock_set_cpu_div().
 *
 * Early warning (optional): enable it with irq_enable(WDG_IRQn). The handler
 * then gets one full period (timeout_ms / 2) to log or save state before the
 * reset. Don't clear the flag in the handler: that is a feed, and it defeats
 * the watchdog. The flag stays set, so the handler must disable WDG_IRQn
 * itself or it re-enters forever:
 *
 *     W80X_IRQ void WDG_IRQHandler(void) { irq_disable(WDG_IRQn); save_state(); }
 *
 * If the code sleeps in __wait(), also call irq_wakeup_enable(WDG_IRQn), or
 * the IRQ can't wake the core and the reset comes without the warning.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_WDG_H
#define W80X_WDG_H

#include "w80x.h"
#include "w80x_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* wm_sdk_w80x's tls_sys_reset() locks the register block. Unlocking an unlocked block does nothing. */
W80X_INLINE void wdg__unlock(void) { WDG->LOCK = WDG_LOCK_KEY; }

/* Start (or restart) with a raw period in APB clocks: IRQ one period and
 * reset two periods after the last feed. */
W80X_INLINE void wdg_start_ticks(uint32_t period)
{
    wdg__unlock();
    WDG->CTRL = 0;
    WDG->CLR  = 1;
    WDG->LD   = period ? period : 1u;
    WDG->CTRL = WDG_CTRL_EN | WDG_CTRL_RST_EN;
}

/* Start (or restart) so that the chip resets at most timeout_ms after the last
 * wdg_feed(). Feed at least every timeout_ms / 2. Max about 214 s at APB 40 MHz. */
W80X_INLINE void wdg_start_ms(uint32_t timeout_ms)
{
    uint64_t period = ((uint64_t)(clock_apb_hz() / 1000u) * timeout_ms) >> 1;
    wdg_start_ticks((period > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)period);
}

/* Feed the watchdog. */
W80X_INLINE void wdg_feed(void)
{
    WDG->CLR = 1;
}

/* Stop the timer; no reset will happen. */
W80X_INLINE void wdg_stop(void)
{
    wdg__unlock();
    WDG->CTRL = 0;
    WDG->CLR  = 1;
}

W80X_INLINE int      wdg_is_running(void)  { return (WDG->CTRL & WDG_CTRL_EN) != 0; }
W80X_INLINE uint32_t wdg_period(void)      { return WDG->LD; }
W80X_INLINE uint32_t wdg_count(void)       { return WDG->VAL; }   /* APB clocks to next timeout */
/* 1 = a period passed without a feed. The next timeout resets the chip. */
W80X_INLINE int      wdg_irq_pending(void) { return WDG->SRC & 1u; }

/* Reset the whole chip right away, using a very short watchdog period
 * (the same trick as wm_sdk_w80x tls_sys_reset() and wm_iot_sdk's WDT HAL). */
__attribute__((noreturn)) W80X_INLINE void wdg_system_reset(void)
{
    __disable_irq();
    wdg__unlock();
    WDG->CTRL = 0;
    WDG->CLR  = 1;
    WDG->LD   = 0x100;
    WDG->CTRL = WDG_CTRL_EN | WDG_CTRL_RST_EN;
    for (;;) { }
}

/* ---- why did we reset? ----------------------------------------------------
 * Returns CLK_RSTSTAT_WDG and/or CLK_RSTSTAT_CPU, or 0 after a power-on or pin
 * reset. The flags stay set until cleared, so clear them once you've read them. */
W80X_INLINE uint32_t reset_status(void)
{
    return CLK->RST_STATUS & (CLK_RSTSTAT_WDG | CLK_RSTSTAT_CPU);
}

W80X_INLINE void reset_status_clear(void)
{
    CLK->RST_STATUS = CLK_RSTSTAT_WDG_CLR | CLK_RSTSTAT_CPU_CLR;
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_WDG_H */
