/*
 * w80x_pwm.h - PWM controller, 5 output channels (manual chapter 23).
 * Header-only.
 *
 *   pwm_init();
 *   pwm_pin(PB25, GPIO_AF2);                 // PWM3 on PB25 (LED1 on the HLK kit)
 *   pwm_setup(PWM3, 1000, PWM_INVERT);       // 1 kHz; returns the rate set
 *   pwm_set_duty(PWM3, PWM_DUTY_MAX / 4);    // 25 %
 *   pwm_start(PWM3);
 *
 * Each channel divides the PWM clock (the APB clock, 40 MHz by default)
 * by a 16-bit divider and counts an 8-bit period:
 *
 *   edge-aligned:   f = APB / (clkdiv * (period + 1)),  high for cmp + 1 counts
 *   center-aligned: f = APB / (clkdiv * 2 * (period + 1)), high for 2 * cmp + 1
 *
 * pwm_setup() picks period 255 (finest duty steps) and the divider for the
 * rate, or a shorter period with divider 1 above APB / 256 (156 kHz at
 * 40 MHz), up to APB / 2. Rates from about 2.4 Hz (40 MHz / 65535 / 256).
 *
 * Duty: pwm_set_duty() takes 0..PWM_DUTY_MAX. The hardware's shortest
 * high time is one count (cmp = 0), so 0 gives 1/(period + 1), not 0 %;
 * stop the channel or use the pin as a GPIO for a steady low.
 * PWM_DUTY_MAX gives a steady high. pwm_set_cmp() sets the raw compare.
 *
 * Pins (W801/W806, wm_sdk_w80x wm_gpio_afsel.c):
 *   PWM0  PB0 AF1, PB12 AF2, PB19 AF2, PA2 AF3, PA10 AF3
 *   PWM1  PB1 AF1, PB13 AF2, PB20 AF2, PA3 AF3, PA11 AF3
 *   PWM2  PB2 AF1, PB14 AF2, PB24 AF2, PA0 AF3, PA12 AF3
 *   PWM3  PB3 AF1, PB15 AF2, PB25 AF2, PA1 AF3, PA13 AF3
 *   PWM4  PA7 AF1, PB16 AF2, PB26 AF2, PA4 AF3, PA14 AF3
 *   brake input  PA8 AF1, PB17 AF2, PA5 AF3, PA15 AF3, PB8 AF3
 * PB19/PB20 are UART0 (the console), and PA1 is JTAG.
 *
 * Pairs: CH0/CH1 and CH2/CH3 can run complementary (CH1 = inverted CH0,
 * optional dead time) or in step (CH1 = CH0); pwm_all_sync() makes every
 * channel follow CH0. The follower takes its waveform from the leader, but
 * both counters must run (pwm_start_mask()).
 *
 * The clock is the APB clock: call pwm_setup() again after a CPU clock
 * change that changes APB (see w80x_clock.h).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_PWM_H
#define W80X_PWM_H

#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* pwm_setup() flags */
#define PWM_EDGE        0x0u     /* edge-aligned (default)                   */
#define PWM_CENTER      0x1u     /* center-aligned: half the rate, symmetric */
#define PWM_INVERT      0x2u     /* output inverted (e.g. active-low LEDs)   */
#define PWM_ONESHOT     0x4u     /* stop after pwm_set_count() periods       */

/* pwm_set_duty() full scale */
#define PWM_DUTY_MAX    0xFFFFu

/* pwm_complementary() dead-time clock: the PWM clock divided by 1, 2, 4, 8 */
#define PWM_DEAD_DIV1   0u
#define PWM_DEAD_DIV2   1u
#define PWM_DEAD_DIV4   2u
#define PWM_DEAD_DIV8   3u

/* Channel pairs for pwm_complementary() / pwm_pair_sync() */
#define PWM_PAIR01      0u       /* CH0 leads, CH1 follows */
#define PWM_PAIR23      1u       /* CH2 leads, CH3 follows */

/* Write an 8-bit field of channel ch (0..3) in a one-byte-per-channel
 * register (PERIOD, PNUM, CMPDAT). */
W80X_INLINE void pwm__set_byte(__IO uint32_t *reg, uint32_t ch, uint32_t value)
{
    uint32_t shift = 8u * ch;
    *reg = (*reg & ~(0xFFUL << shift)) | ((value & 0xFFu) << shift);
}

/* Set or clear bits in a register. */
W80X_INLINE void pwm__bits(__IO uint32_t *reg, uint32_t bits, int on)
{
    if (on) {
        *reg |= bits;
    } else {
        *reg &= ~bits;
    }
}

/* Clock on and the controller out of reset (all channels stopped). */
W80X_INLINE void pwm_init(void)
{
    clock_enable(CLK_GATE_PWM);
    periph_reset(CLK_RST_PWM);
}

/* Route a pin to its PWM function (see the table above). */
W80X_INLINE void pwm_pin(uint8_t pin, gpio_af_t af)
{
    gpio_af(pin, af);
}

W80X_INLINE void pwm_start(uint32_t ch)
{
    PWM->CTL |= PWM_CTL_CNTEN(ch);
}

W80X_INLINE void pwm_stop(uint32_t ch)
{
    PWM->CTL &= ~PWM_CTL_CNTEN(ch);
}

/* Start or stop several channels at once: bit n = channel n. Channels
 * started in one write begin in step. */
W80X_INLINE void pwm_start_mask(uint32_t mask)
{
    PWM->CTL |= (mask << PWM_CTL_CNTEN_Pos) & PWM_CTL_CNTEN_Msk;
}

W80X_INLINE void pwm_stop_mask(uint32_t mask)
{
    PWM->CTL &= ~((mask << PWM_CTL_CNTEN_Pos) & PWM_CTL_CNTEN_Msk);
}

W80X_INLINE int pwm_running(uint32_t ch)
{
    return (PWM->CTL & PWM_CTL_CNTEN(ch)) != 0u;
}

/* Raw clock divider (1..65535; 0 also means 1) and period (1..255). */
W80X_INLINE void pwm_set_clock(uint32_t ch, uint32_t clkdiv, uint32_t period)
{
    clkdiv &= PWM_CLKDIV_Msk;
    period &= PWM_PERIOD_MAX;
    if (ch == PWM4) {
        PWM->CH4_REG1 = (PWM->CH4_REG1 & PWM_CH4_PNUM_Msk) | (period << PWM_CH4_PERIOD_Pos) |
                        (clkdiv << PWM_CH4_CLKDIV_Pos);
    } else {
        __IO uint32_t *div_reg = (ch < PWM2) ? &PWM->CLKDIV01 : &PWM->CLKDIV23;
        uint32_t shift = 16u * (ch & 1u);        /* even channel low half, odd high */
        *div_reg = (*div_reg & ~((uint32_t)PWM_CLKDIV_Msk << shift)) | (clkdiv << shift);
        pwm__set_byte(&PWM->PERIOD, ch, period);
    }
}

W80X_INLINE uint32_t pwm_get_period(uint32_t ch)
{
    if (ch == PWM4) {
        return (PWM->CH4_REG1 & PWM_CH4_PERIOD_Msk) >> PWM_CH4_PERIOD_Pos;
    }
    return (PWM->PERIOD >> (8u * ch)) & PWM_PERIOD_MAX;
}

W80X_INLINE uint32_t pwm_get_clkdiv(uint32_t ch)
{
    uint32_t clkdiv;
    if (ch == PWM4) {
        clkdiv = (PWM->CH4_REG1 & PWM_CH4_CLKDIV_Msk) >> PWM_CH4_CLKDIV_Pos;
    } else {
        uint32_t regval = (ch < PWM2) ? PWM->CLKDIV01 : PWM->CLKDIV23;
        clkdiv = (regval >> (16u * (ch & 1u))) & PWM_CLKDIV_Msk;
    }
    return clkdiv ? clkdiv : 1u;
}

W80X_INLINE int pwm__is_center(uint32_t ch)
{
    uint32_t type;
    if (ch == PWM4) {
        type = (PWM->CH4_REG2 & PWM_CH4_CNTTYPE_Msk) >> PWM_CH4_CNTTYPE_Pos;
    } else {
        type = (PWM->CTL & PWM_CTL_CNTTYPE_Msk(ch)) >> PWM_CTL_CNTTYPE_Pos(ch);
    }
    return type == PWM_CNTTYPE_CENTER;
}

/* Output rate of channel ch, in Hz, from its registers. */
W80X_INLINE uint32_t pwm_get_freq(uint32_t ch)
{
    uint32_t counts = pwm_get_clkdiv(ch) * (pwm_get_period(ch) + 1u);
    if (pwm__is_center(ch)) {
        counts *= 2u;
    }
    return (clock_apb_hz() + (counts / 2u)) / counts;   /* rounded */
}

/* Raw compare value: edge-aligned high for cmp + 1 counts (cmp >= period:
 * always high); center-aligned high for 2 * cmp + 1 (cmp > period: always
 * high). Before PWM_INVERT. */
W80X_INLINE void pwm_set_cmp(uint32_t ch, uint32_t cmp)
{
    if (ch == PWM4) {
        PWM->CH4_REG2 = (PWM->CH4_REG2 & ~PWM_CH4_CMP_Msk) | ((cmp << PWM_CH4_CMP_Pos) & PWM_CH4_CMP_Msk);
    } else {
        pwm__set_byte(&PWM->CMPDAT, ch, cmp);
    }
}

/* Duty 0..PWM_DUTY_MAX of the channel's period (see the top of the file:
 * 0 is one count high, PWM_DUTY_MAX is a steady high). */
W80X_INLINE void pwm_set_duty(uint32_t ch, uint32_t duty)
{
    uint32_t period = pwm_get_period(ch);
    /* edge: cmp 0..period; center: cmp 0..period + 1 (above period = high) */
    uint32_t full = pwm__is_center(ch) ? (period + 1u) : period;
    if (duty > PWM_DUTY_MAX) {
        duty = PWM_DUTY_MAX;
    }
    pwm_set_cmp(ch, ((duty * full) + (PWM_DUTY_MAX / 2u)) / PWM_DUTY_MAX);
}

/* Invert the output (before complementary mode and brake). */
W80X_INLINE void pwm_set_invert(uint32_t ch, int on)
{
    if (ch == PWM4) {
        pwm__bits(&PWM->CH4_REG2, PWM_CH4_PINV, on);
    } else {
        pwm__bits(&PWM->CTL, PWM_CTL_PINV(ch), on);
    }
}

/* Periods to generate in PWM_ONESHOT mode (1..255), after which the
 * channel stops by itself (its pwm_running() goes to 0) and sets its
 * pwm_irq_status() bit. In auto-reload mode the
 * bit is set every count periods (to check). */
W80X_INLINE void pwm_set_count(uint32_t ch, uint32_t count)
{
    if (ch == PWM4) {
        PWM->CH4_REG1 = (PWM->CH4_REG1 & ~PWM_CH4_PNUM_Msk) | ((count << PWM_CH4_PNUM_Pos) & PWM_CH4_PNUM_Msk);
    } else {
        pwm__set_byte(&PWM->PNUM, ch, count);
    }
}

/* Set up channel ch as an output at hz: stops it, picks the divider and
 * period, sets the counter type, mode and polarity, duty 50 %. Returns the
 * rate set, rounded to Hz (0 if hz is outside about 2.4 Hz .. APB / 2).
 * Start it with pwm_start(). */
static inline uint32_t pwm_setup(uint32_t ch, uint32_t hz, uint32_t flags)
{
    uint32_t apb = clock_apb_hz();
    uint32_t center = (flags & PWM_CENTER) ? 1u : 0u;
    uint32_t steps = center ? 2u : 1u;           /* counter steps per count */
    /* center-aligned: the manual says not to use period 255 */
    uint32_t max_period = center ? (PWM_PERIOD_MAX - 1u) : PWM_PERIOD_MAX;
    uint32_t type = center ? PWM_CNTTYPE_CENTER : PWM_CNTTYPE_DOWN;
    uint32_t autoload = (flags & PWM_ONESHOT) ? 0u : 1u;
    uint32_t clkdiv = 1u;
    uint32_t period = max_period;

    if ((hz == 0u) || (hz > (apb / (2u * steps)))) {
        return 0;
    }
    uint32_t counts = (apb + ((hz * steps) / 2u)) / (hz * steps);  /* clocks per period */
    if (counts <= (max_period + 1u)) {
        period = counts - 1u;
    } else {
        clkdiv = (counts + ((max_period + 1u) / 2u)) / (max_period + 1u);
        if (clkdiv > PWM_CLKDIV_Msk) {
            return 0;                            /* slower than the slowest rate */
        }
    }

    pwm_stop(ch);
    /* mode first: the manual says changing CNTMODE clears the compare */
    if (ch == PWM4) {
        PWM->CAP2CTL &= ~(PWM_CAP2_CPEN | PWM_CAP2_POEN);
        PWM->CH4_REG2 = (PWM->CH4_REG2 & ~(PWM_CH4_CNTTYPE_Msk | PWM_CH4_CNTMODE)) |
                        (type << PWM_CH4_CNTTYPE_Pos) | (autoload ? PWM_CH4_CNTMODE : 0u);
    } else {
        uint32_t ctl = PWM->CTL & ~(PWM_CTL_CNTTYPE_Msk(ch) | PWM_CTL_CNTMODE(ch));
        if (ch == PWM0) {
            ctl &= ~(PWM_CTL_CPEN | PWM_CTL_POEN);
        }
        PWM->CTL = ctl | (type << PWM_CTL_CNTTYPE_Pos(ch)) | (autoload ? PWM_CTL_CNTMODE(ch) : 0u);
    }
    pwm_set_clock(ch, clkdiv, period);
    pwm_set_invert(ch, (flags & PWM_INVERT) != 0u);
    pwm_set_duty(ch, PWM_DUTY_MAX / 2u);
    return pwm_get_freq(ch);
}

/* Complementary pair (PWM_PAIR01 / PWM_PAIR23): the follower outputs the
 * inverse of the leader, with dead_clocks (0..255) of dead time in clocks
 * of the PWM clock / div (PWM_DEAD_DIV*; shared by both pairs). 0 = no
 * dead time. Set the leader up with pwm_setup(), then start both. */
W80X_INLINE void pwm_complementary(uint32_t pair, uint32_t dead_clocks, uint32_t div)
{
    uint32_t dtctl = PWM->DTCTL & ~(PWM_DTCTL_CNT_Msk(pair) | PWM_DTCTL_DIV_Msk | PWM_DTCTL_EN(pair));
    dtctl |= ((dead_clocks << PWM_DTCTL_CNT_Pos(pair)) & PWM_DTCTL_CNT_Msk(pair)) |
             ((div << PWM_DTCTL_DIV_Pos) & PWM_DTCTL_DIV_Msk);
    if (dead_clocks != 0u) {
        dtctl |= PWM_DTCTL_EN(pair);
    }
    PWM->DTCTL = dtctl;
    PWM->CTL = (PWM->CTL & ~(PWM_CTL_TWOSYNC(pair) | PWM_CTL_ALLSYNC)) | PWM_CTL_OUTMODE_COMP(pair);
}

/* Pair in step: the follower outputs the same waveform as the leader. */
W80X_INLINE void pwm_pair_sync(uint32_t pair)
{
    PWM->DTCTL &= ~PWM_DTCTL_EN(pair);
    PWM->CTL = (PWM->CTL & ~(PWM_CTL_OUTMODE_COMP(pair) | PWM_CTL_ALLSYNC)) | PWM_CTL_TWOSYNC(pair);
}

/* Every channel outputs CH0's waveform. */
W80X_INLINE void pwm_all_sync(void)
{
    PWM->CTL = (PWM->CTL & ~(PWM_CTL_OUTMODE_COMP(0) | PWM_CTL_OUTMODE_COMP(1) |
                             PWM_CTL_TWOSYNC(0) | PWM_CTL_TWOSYNC(1))) | PWM_CTL_ALLSYNC;
}

/* Back to five independent channels. */
W80X_INLINE void pwm_independent(void)
{
    PWM->CTL &= ~(PWM_CTL_OUTMODE_COMP(0) | PWM_CTL_OUTMODE_COMP(1) | PWM_CTL_TWOSYNC(0) |
                  PWM_CTL_TWOSYNC(1) | PWM_CTL_ALLSYNC);
    PWM->DTCTL &= ~(PWM_DTCTL_EN(0) | PWM_DTCTL_EN(1));
}

/* Brake: while the brake input pin is active, channel ch outputs high
 * (1) or low (0) instead. on = 0 turns braking off for the channel. */
W80X_INLINE void pwm_brake(uint32_t ch, int on, int high)
{
    pwm__bits(&PWM->BRKCTL, PWM_BRKCTL_LEVEL(ch), high);
    pwm__bits(&PWM->BRKCTL, PWM_BRKCTL_EN(ch), on);
}

/* Period-count interrupt (PWM_IRQn, shared): set when the PWM_ONESHOT
 * count is done. Bit n of status/clear = channel n. */
W80X_INLINE void pwm_irq_enable(uint32_t ch, int on)
{
    pwm__bits(&PWM->INTEN, PWM_INTEN_PIEN(ch), on);
}

W80X_INLINE uint32_t pwm_irq_status(void)
{
    return PWM->INTSTS & PWM_INTSTS_PIF_ALL;
}

W80X_INLINE void pwm_irq_clear(uint32_t mask)
{
    PWM->INTSTS = mask & PWM_INTSTS_PIF_ALL;
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_PWM_H */
