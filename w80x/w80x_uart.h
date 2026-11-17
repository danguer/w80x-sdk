/*
 * w80x_uart.h - UART0..UART5, polled. Header-only.
 *
 *   uart0_init(115200);              // PB19 = TX, PB20 = RX (the USB-serial port)
 *   uart_puts(UART0, "hello\r\n");
 *   int ch = uart_getc(UART0);       // blocks
 *
 * printf: link-time hooks in w80x_stdio.c send stdout/stderr to a UART and
 * read stdin from it. Pick the UART once:
 *
 *   uart0_init(115200);
 *   stdio_set_uart(UART0);           // "\n" goes out as "\r\n"
 *   printf("clock %lu Hz\n", SystemCoreClock);
 *
 * Other UARTs: route the pins, then init. Pin options (wm_sdk_w80x
 * wm_gpio_afsel.c; see the datasheet's pin table for the full list):
 *
 *   UART0  TX PB19 (AF1) or PB27 (AF2), RX PB20 (AF1)
 *   UART1  TX PB6  (AF1),               RX PB7  (AF1)
 *
 *   uart_pins(PB6, PB7, GPIO_AF1);
 *   uart_init(UART1, 115200);
 *
 * The baud divider is computed from the APB clock when you call uart_init()
 * or uart_set_baud(). Some CPU clocks lower APB (see w80x_clock.h), so call
 * uart_set_baud() again after clock_set_cpu_div(), and uart_flush() before it.
 *
 * Interrupts: uart_irq_enable(uart, UART_INT_RX_LEVEL | UART_INT_RX_TIMEOUT)
 * and irq_enable(UART0_IRQn). In the handler, read the data, then clear the
 * flags with uart_irq_clear(uart, uart_irq_status(uart)). UART3, UART4 and
 * UART5 share UART3_5_IRQn.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_UART_H
#define W80X_UART_H

#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TX FIFO limit used by uart_putc(). Per the datasheet, the TX/RX data windows
 * accept bursts of up to 16 bytes, so the vendor SDK (and this driver) treat
 * 16 bytes in the TX FIFO as full. */
#define UART_TX_FIFO_SIZE  16u

/* Line formats for uart_set_format(). */
#define UART_8N1  (UART_LC_DATA_8)
#define UART_8E1  (UART_LC_DATA_8 | UART_LC_PARITY_EN)
#define UART_8O1  (UART_LC_DATA_8 | UART_LC_PARITY_EN | UART_LC_PARITY_ODD)
#define UART_8N2  (UART_LC_DATA_8 | UART_LC_STOP_2)

/* 0..5 from the register block address. */
W80X_INLINE uint32_t uart_index(UART_TypeDef *uart)
{
    return ((uint32_t)(uintptr_t)uart - UART0_BASE) / (UART1_BASE - UART0_BASE);
}

/* Route a TX/RX pin pair to the UART. RX gets the pull-up (as the vendor
 * SDK does), so a floating line doesn't produce garbage. Pass GPIO_NO_PIN to
 * skip a pin. */
W80X_INLINE void uart_pins(uint8_t tx, uint8_t rx, gpio_af_t af)
{
    if (tx != GPIO_NO_PIN) {
        gpio_af(tx, af);
    }
    if (rx != GPIO_NO_PIN) {
        gpio_af(rx, af);
        gpio__rmw(&gpio_port(rx)->PULL_EN, gpio_mask(rx), 0);   /* 0 = pull-up on */
        gpio__rmw(&gpio_port(rx)->DN_EN, gpio_mask(rx), 0);
    }
}

/* divider = APB / (16 * baud) - 1, fraction = remainder in 1/16ths (the
 * vendor formula). Returns the baud rate actually produced. */
W80X_INLINE uint32_t uart_set_baud(UART_TypeDef *uart, uint32_t baud)
{
    uint32_t apb = clock_apb_hz();
    uint32_t b16 = baud * 16u;
    uint32_t div = apb / b16;
    uint32_t frac = ((apb % b16) * 16u) / b16;

    if (div == 0) {                       /* too fast for this APB clock */
        div = 1;
        frac = 0;
    }
    uart->BAUDR = ((div - 1u) & UART_BAUDR_DIV_Msk) | (frac << UART_BAUDR_FRAC_Pos);
    return apb / ((16u * div) + frac);    /* one bit = 16 * div + frac APB clocks */
}

/* Data bits / parity / stop bits; keeps TX and RX enabled. */
W80X_INLINE void uart_set_format(UART_TypeDef *uart, uint32_t fmt)
{
    uart->LC = (uart->LC & (UART_LC_TX_EN | UART_LC_RX_EN)) | fmt;
}

/* Clock on, out of reset, 8N1, TX + RX on, FIFOs empty, no interrupts.
 * Doesn't touch the pins: see uart_pins() or uart0_init(). */
W80X_INLINE void uart_init(UART_TypeDef *uart, uint32_t baud)
{
    uint32_t index = uart_index(uart);

    CLK->GATE_EN  |= CLK_GATE_UART0 << index;
    CLK->RST_CTRL |= CLK_RST_UART0 << index;

    uart->LC    = 0;
    uart->INTM  = UART_INT_ALL;
    uart->FC    = 0;
    uart->DMAC  = 0;
    uart->FIFOC = UART_FIFOC_TX_RST | UART_FIFOC_RX_RST;
    uart->FIFOC = 0;
    uart_set_baud(uart, baud);
    uart->INTS  = 0xFFFFFFFFu;
    uart->LC    = UART_8N1 | UART_LC_TX_EN | UART_LC_RX_EN;
}

/* UART0 on the default pins (PB19 TX, PB20 RX): the port the bootloader
 * uses, wired to the USB-serial chip on most boards. */
W80X_INLINE void uart0_init(uint32_t baud)
{
    uart_pins(PB19, PB20, GPIO_AF1);
    uart_init(UART0, baud);
}

/* ---- status ---------------------------------------------------------- */

W80X_INLINE uint32_t uart_tx_count(UART_TypeDef *uart) { return uart->FIFOS & UART_FIFOS_TX_Msk; }
W80X_INLINE uint32_t uart_rx_count(UART_TypeDef *uart) { return (uart->FIFOS & UART_FIFOS_RX_Msk) >> UART_FIFOS_RX_Pos; }
W80X_INLINE int      uart_rx_ready(UART_TypeDef *uart) { return uart_rx_count(uart) != 0; }
W80X_INLINE int      uart_tx_ready(UART_TypeDef *uart) { return uart_tx_count(uart) < UART_TX_FIFO_SIZE; }

/* ---- transmit ---------------------------------------------------------- */

W80X_INLINE void uart_putc(UART_TypeDef *uart, char ch)
{
    while (!uart_tx_ready(uart)) {
    }
    uart->TDW = (uint8_t)ch;
}

W80X_INLINE void uart_write(UART_TypeDef *uart, const void *buf, uint32_t len)
{
    const uint8_t *pos = (const uint8_t *)buf;
    while (len--) {
        uart_putc(uart, (char)*pos++);
    }
}

/* No newline translation: write "\r\n" yourself (printf adds the \r). */
W80X_INLINE void uart_puts(UART_TypeDef *uart, const char *str)
{
    while (*str) {
        uart_putc(uart, *str++);
    }
}

/* Wait until everything is on the wire: FIFO empty, then one more character
 * time for the byte in the shift register (there is no "TX idle" flag). Use
 * it before a reset, a clock change or sleeping. */
W80X_INLINE void uart_flush(UART_TypeDef *uart)
{
    while (uart_tx_count(uart)) {
    }
    uint32_t baudr = uart->BAUDR;
    uint32_t bit_apb = (16u * ((baudr & UART_BAUDR_DIV_Msk) + 1u))
                     + ((baudr & UART_BAUDR_FRAC_Msk) >> UART_BAUDR_FRAC_Pos);
    uint32_t ratio = clock_bus2_div();
    delay_ticks(12u * bit_apb * (ratio ? ratio : 1u));   /* 12 bits: up to 8E2 */
}

/* ---- receive ----------------------------------------------------------- */

/* Next byte, or -1 if none is waiting. */
W80X_INLINE int uart_try_getc(UART_TypeDef *uart)
{
    return uart_rx_ready(uart) ? (int)(uart->RDW & 0xFFu) : -1;
}

W80X_INLINE int uart_getc(UART_TypeDef *uart)
{
    while (!uart_rx_ready(uart)) {
    }
    return (int)(uart->RDW & 0xFFu);
}

W80X_INLINE void uart_rx_flush(UART_TypeDef *uart)
{
    uart->FIFOC |= UART_FIFOC_RX_RST;
}

/* Sticky receive errors (UART_INT_FRAME_ERR, _PARITY_ERR, _OVERRUN,
 * _BREAK). They are set even while masked; uart_irq_clear() clears them. */
W80X_INLINE uint32_t uart_rx_errors(UART_TypeDef *uart) { return uart->INTS & UART_INT_RX_ERRORS; }

/* ---- interrupts ----------------------------------------------------------- */

/* FIFO trigger levels for uart_set_rx_level() / uart_set_tx_level().
 * The same field value means slightly different things for each FIFO: the
 * lowest TX level is "FIFO empty", not 1 byte. */
#define UART_INT_LEVEL_1BYTE   0u   /* RX: 1 byte received; TX: FIFO empty */
#define UART_INT_LEVEL_EMPTY   0u   /* TX alias of UART_INT_LEVEL_1BYTE    */
#define UART_INT_LEVEL_4BYTE   1u
#define UART_INT_LEVEL_8BYTE   2u
#define UART_INT_LEVEL_16BYTE  3u

/* UART_INT_RX_LEVEL fires once the RX FIFO holds at least this many bytes.
 * Add UART_INT_RX_TIMEOUT to also catch a shorter tail when the line goes idle. */
W80X_INLINE void uart_set_rx_level(UART_TypeDef *uart, uint32_t level)
{
    uart->FIFOC = (uart->FIFOC & ~(3u << UART_FIFOC_RXLVL_Pos)) | ((level & 3u) << UART_FIFOC_RXLVL_Pos);
}

/* UART_INT_TX_LEVEL fires once the TX FIFO has drained down to this level,
 * i.e. there is room to refill it. */
W80X_INLINE void uart_set_tx_level(UART_TypeDef *uart, uint32_t level)
{
    uart->FIFOC = (uart->FIFOC & ~(3u << UART_FIFOC_TXLVL_Pos)) | ((level & 3u) << UART_FIFOC_TXLVL_Pos);
}

W80X_INLINE void     uart_irq_enable(UART_TypeDef *uart, uint32_t mask)  { uart->INTM &= ~mask; }
W80X_INLINE void     uart_irq_disable(UART_TypeDef *uart, uint32_t mask) { uart->INTM |= mask; }
W80X_INLINE uint32_t uart_irq_status(UART_TypeDef *uart)                 { return uart->INTS & ~uart->INTM & UART_INT_ALL; }
W80X_INLINE void     uart_irq_clear(UART_TypeDef *uart, uint32_t mask)   { uart->INTS = mask; }

/* ---- stdio (w80x_stdio.c) ------------------------------------------------- */

/* Send printf/puts/stdout/stderr to uart and read stdin from it. NULL (the
 * default) discards output and makes reads return end-of-file. */
void stdio_set_uart(UART_TypeDef *uart);

#ifdef __cplusplus
}
#endif

#endif /* W80X_UART_H */
