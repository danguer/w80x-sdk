/*
 * uart_printf - printf over UART0, plus reading keys from the terminal.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). UART0 (PB19/PB20)
 * goes to the USB-serial chip, the same port used for flashing:
 *
 *     make flash && make monitor        (115200 baud)
 *
 * Prints the clocks and why the chip reset, then a line every second while
 * LED1 blinks. Keys:
 *     + / -   faster / slower CPU clock (the baud rate is recalculated)
 *     other   prints the key code
 */
#include <stdio.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "w80x_uart.h"
#include "w80x_wdg.h"                    /* reset_status() */
#include "boards/hlk_w801_kit.h"

static const clk_cpu_div_t speeds[] = {
    CLK_CPU_240MHZ, CLK_CPU_160MHZ, CLK_CPU_80MHZ, CLK_CPU_40MHZ, CLK_CPU_12MHZ,
};
#define NSPEEDS (sizeof(speeds) / sizeof(speeds[0]))

static void print_clocks(void)
{
    printf("CPU %lu Hz, APB %lu Hz (%.1f MHz)\n",
           (unsigned long)clock_cpu_hz(), (unsigned long)clock_apb_hz(),
           clock_cpu_hz() / 1e6f);
}

static void set_speed(unsigned i)
{
    uart_flush(UART0);                   /* finish sending at the old rate */
    clock_set_cpu_div(speeds[i]);
    uart_set_baud(UART0, 115200);        /* APB may have changed */
    print_clocks();
}

int main(void)
{
    unsigned speed = 0;
    clock_set_cpu_div(speeds[speed]);

    led_init(LED1);
    uart0_init(115200);
    stdio_set_uart(UART0);

    printf("\n\nHello from the %s\n", BOARD_NAME);
    print_clocks();
    printf("reset cause: %s\n", (reset_status() & CLK_RSTSTAT_WDG) ? "watchdog" : "power-on / RESET");
    reset_status_clear();
    printf("keys: + faster, - slower\n");

    uint32_t seconds = 0;
    for (;;) {
        /* ~1 s, checking for keys every 10 ms */
        for (int poll = 0; poll < 100; poll++) {
            int ch = uart_try_getc(UART0);
            if ((ch == '+') && (speed > 0)) {
                set_speed(--speed);
            } else if ((ch == '-') && (speed < (NSPEEDS - 1))) {
                set_speed(++speed);
            } else if (ch >= 0) {
                printf("key '%c' = 0x%02X\n", ((ch >= 32) && (ch < 127)) ? ch : '?', ch);
            }
            delay_ms(10);
        }
        led_toggle(LED1);
        printf("uptime %lu s\n", (unsigned long)++seconds);
    }
}
