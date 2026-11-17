/*
 * dma - memory-to-memory copy and UART output by DMA.
 *
 * Board: HLK-W801-KIT-V1.1 (see boards/hlk_w801_kit.h). Watch the output
 * with `make monitor` (115200 baud).
 *
 *   1. Copies 16 KB with dma_memcpy(), checks it, and compares the time
 *      with memcpy().
 *   2. Sends a block of text over UART0 by DMA while the CPU keeps counting;
 *      the transfer-done interrupt lights LED2.
 *   3. Repeats every 2 s. LED1 is on while the copy runs; it blinks fast if
 *      the check fails.
 */
#include <stdio.h>
#include <string.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_utils.h"
#include "w80x_gpio.h"
#include "w80x_uart.h"
#include "w80x_dma.h"
#include "boards/hlk_w801_kit.h"

#define COPY_CH   0
#define UART_CH   1
#define COPY_LEN  (16u * 1024u)

/* dma_memcpy() uses 32-bit items only if both addresses and the length are
 * multiples of 4: uint32_t arrays are, and the length is checked here. */
_Static_assert((COPY_LEN % 4u) == 0, "COPY_LEN must be a multiple of 4 for 32-bit DMA");
static uint32_t src[COPY_LEN / 4] __attribute__((aligned(4)));
static uint32_t dst[COPY_LEN / 4] __attribute__((aligned(4)));
static char     msg[256];               /* DMA source must be in SRAM */

static volatile int uart_dma_done;

W80X_IRQ void DMA_Channel1_IRQHandler(void)
{
    dma_irq_clear(UART_CH, DMA_IRQ_ALL);
    dma_uart_tx_end(UART0);
    led_on(LED2);
    uart_dma_done = 1;
}

/* CORET counts down at the CPU clock; 24 bits, fine for < 70 ms at 240 MHz. */
static void fail(void)
{
    printf("FAILED\n");
    for (;;) {
        led_toggle(LED1);
        delay_ms(100);
    }
}

int main(void)
{
    clock_set_cpu_div(CLK_CPU_240MHZ);
    led_init(LED1);
    led_init(LED2);
    uart0_init(115200);
    stdio_set_uart(UART0);
    dma_init();

    dma_irq_enable(UART_CH, DMA_IRQ_DONE);
    irq_enable(dma_irqn(UART_CH));
    __enable_irq();

    printf("\n\nDMA demo on the %s\n", BOARD_NAME);

    for (uint32_t round = 1;; round++) {
        /* 1. memory to memory */
        for (uint32_t i = 0; i < (COPY_LEN / 4); i++) {
            src[i] = (i * 2654435761u) + round;
            dst[i] = 0;
        }

        led_on(LED1);
        uint32_t t0 = CPU_TICKS;
        dma_memcpy(COPY_CH, dst, src, COPY_LEN);
        uint32_t t_dma = cpu_cycles_since(t0);
        led_off(LED1);

        if (memcmp(dst, src, COPY_LEN) != 0) {
            fail();
        }

        /* Read back which item width the hardware actually used. */
        static const char *const widths[] = { "8", "16", "32", "?" };
        uint32_t ctrl = dma_ch(COPY_CH)->CTRL;
        const char *width = widths[(ctrl >> DMA_CTRL_WIDTH_Pos) & 3u];
        const char *burst = (ctrl & DMA_CTRL_BURST4) ? "4" : "1";

        /* The same copy with 8-bit items, for comparison. */
        t0 = CPU_TICKS;
        dma_setup(COPY_CH, dst, src, COPY_LEN, DMA_SRC_INC | DMA_DST_INC | DMA_WIDTH_8, DMA_REQ_MEM);
        dma_start(COPY_CH);
        dma_wait(COPY_CH);
        uint32_t t_dma8 = cpu_cycles_since(t0);

        t0 = CPU_TICKS;
        memcpy(dst, src, COPY_LEN);
        uint32_t t_cpu = cpu_cycles_since(t0);

        printf("round %lu: %u bytes (src %p, dst %p)\n",
               (unsigned long)round, COPY_LEN, (void *)src, (void *)dst);
        printf("  DMA %s-bit, burst %s: %lu cycles\n", width, burst, (unsigned long)t_dma);
        printf("  DMA 8-bit:            %lu cycles\n", (unsigned long)t_dma8);
        printf("  memcpy:               %lu cycles\n", (unsigned long)t_cpu);
        fflush(stdout);
        uart_flush(UART0);

        /* 2. UART TX by DMA while the CPU does something else */
        int msg_len = snprintf(msg, sizeof msg,
                         "  this line went out by DMA (round %lu); the CPU counted meanwhile: ",
                         (unsigned long)round);
        led_off(LED2);
        uart_dma_done = 0;
        dma_uart_tx(UART_CH, UART0, msg, (uint32_t)msg_len);

        uint32_t spins = 0;
        while (!uart_dma_done) {
            spins++;
        }
        uart_flush(UART0);
        printf("%lu loops\n", (unsigned long)spins);

        delay_ms(2000);
    }
}
