/*
 * pwm - PWM output: LED fading, counted pulses, and a self-check.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). Output on UART0,
 * 115200 baud (`make monitor`).
 *
 *   1. Prints the rate pwm_setup() picks for a range of requests, with the
 *      divider and period, and checks it against the formula.
 *   2. LED2 (PB26, PWM4): 5 one-shot pulses at 4 Hz every 3 s; the
 *      period-count interrupt prints when they are done.
 *   3. LED1 fades in and out at 1 kHz (PWM_INVERT: the LEDs are active low,
 *      so the duty is the time the LED is on).
 */
#include <stdio.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_utils.h"
#include "w80x_gpio.h"
#include "w80x_uart.h"
#include "w80x_pwm.h"
#include "boards/hlk_w801_kit.h"

#define FADE_CH          PWM3            /* LED1 = PB25, AF2 */
#define FADE_AF          GPIO_AF2
#define FADE_HZ          1000u
#define FADE_STEPS       64u
#define FADE_STEP_MS     15u

#define PULSE_CH         PWM4            /* LED2 = PB26, AF2 */
#define PULSE_AF         GPIO_AF2
#define PULSE_HZ         4u
#define PULSE_COUNT      5u
#define PULSE_EVERY_MS   3000u

static volatile uint32_t pulse_bursts_done;

W80X_IRQ void PWM_IRQHandler(void)
{
    uint32_t fired = pwm_irq_status();
    pwm_irq_clear(fired);
    if (fired & BIT(PULSE_CH)) {
        pulse_bursts_done++;
    }
}

/* Requested rates and what pwm_setup() makes of them. */
static void show_rates(void)
{
    static const uint32_t requests[] = { 1u, 3u, 50u, 1000u, 20000u, 156250u, 200000u,
                                         1000000u, 10000000u, 20000000u, 30000000u };
    uint32_t apb = clock_apb_hz();
    uint32_t failed = 0;

    printf("Rates (APB %lu Hz):\n", (unsigned long)apb);
    printf("  %10s %10s %7s %7s %6s\n", "request", "set", "clkdiv", "period", "check");
    for (uint32_t i = 0; i < (sizeof requests / sizeof requests[0]); i++) {
        uint32_t set = pwm_setup(FADE_CH, requests[i], PWM_EDGE);
        uint32_t clkdiv = pwm_get_clkdiv(FADE_CH);
        uint32_t period = pwm_get_period(FADE_CH);
        int ok = (set == 0u) || (set == pwm_get_freq(FADE_CH));
        failed += ok ? 0u : 1u;
        if (set == 0u) {
            /* rejected: the channel was not changed */
            printf("  %10lu %10s %7s %7s %6s\n", (unsigned long)requests[i], "rejected", "-", "-", "ok");
            continue;
        }
        printf("  %10lu %10lu %7lu %7lu %6s\n", (unsigned long)requests[i], (unsigned long)set,
               (unsigned long)clkdiv, (unsigned long)period, ok ? "ok" : "FAIL");
    }
    uint32_t center = pwm_setup(FADE_CH, FADE_HZ, PWM_CENTER);
    printf("  center-aligned %lu Hz: set %lu, clkdiv %lu, period %lu\n", (unsigned long)FADE_HZ,
           (unsigned long)center, (unsigned long)pwm_get_clkdiv(FADE_CH),
           (unsigned long)pwm_get_period(FADE_CH));
    printf("Rate check: %lu failed\n\n", (unsigned long)failed);
}

int main(void)
{
    clock_set_cpu_div(CLK_CPU_240MHZ);
    uart0_init(115200);
    stdio_set_uart(UART0);
    printf("\n\nPWM demo on the %s, CPU %lu Hz\n\n", BOARD_NAME, (unsigned long)clock_cpu_hz());

    pwm_init();
    pwm_pin(LED1, FADE_AF);
    pwm_pin(LED2, PULSE_AF);

    show_rates();

    /* counted pulses on LED2: one-shot, PULSE_COUNT periods, then the interrupt */
    uint32_t pulse_hz = pwm_setup(PULSE_CH, PULSE_HZ, PWM_ONESHOT | PWM_INVERT);
    pwm_set_duty(PULSE_CH, PWM_DUTY_MAX / 4u);
    pwm_set_count(PULSE_CH, PULSE_COUNT);
    pwm_irq_clear(BIT(PULSE_CH));
    pwm_irq_enable(PULSE_CH, 1);
    irq_enable(PWM_IRQn);
    __enable_irq();
    printf("LED2: %lu pulses at %lu Hz every %lu ms\n", (unsigned long)PULSE_COUNT,
           (unsigned long)pulse_hz, (unsigned long)PULSE_EVERY_MS);

    uint32_t fade_hz = pwm_setup(FADE_CH, FADE_HZ, PWM_INVERT);
    pwm_set_duty(FADE_CH, 0);
    pwm_start(FADE_CH);
    printf("LED1: fading at %lu Hz\n", (unsigned long)fade_hz);

    uint32_t step = 0;
    uint32_t since_pulses = PULSE_EVERY_MS;
    uint32_t bursts_seen = 0;
    while (1) {
        /* triangle 0..FADE_STEPS..0, squared so the brightness looks even */
        uint32_t phase = step % (2u * FADE_STEPS);
        uint32_t level = (phase < FADE_STEPS) ? phase : ((2u * FADE_STEPS) - phase);
        pwm_set_duty(FADE_CH, (level * level * PWM_DUTY_MAX) / (FADE_STEPS * FADE_STEPS));
        step++;

        if (since_pulses >= PULSE_EVERY_MS) {
            since_pulses = 0;
            pwm_start(PULSE_CH);         /* the counter stops itself after the count */
        }
        if (bursts_seen != pulse_bursts_done) {
            bursts_seen = pulse_bursts_done;
            printf("LED2: burst %lu done, counter %s\n", (unsigned long)bursts_seen,
                   pwm_running(PULSE_CH) ? "still running" : "stopped");
        }
        delay_ms(FADE_STEP_MS);
        since_pulses += FADE_STEP_MS;
    }
}
