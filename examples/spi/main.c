/*
 * spi - SPI master loopback self-test.
 *
 * Board: HLK-W801-KIT-V1.1. Output on UART0, 115200 baud (`make monitor`).
 *
 * WIRING: connect MOSI (PA7) to MISO (PB3) with one jumper wire. Every byte
 * sent then comes straight back, so the test needs no SPI device.
 * SCK is on PB2 (scope it if you like). Optional second wire: CS (PB4) to
 * PB7, to check spi_cs(); without it that check is skipped.
 *
 * Checks: lengths that are not multiples of 4 or of the 32-byte FIFO, all
 * four clock modes, clock rates from 100 kHz to 20 MHz (with throughput),
 * spi_read()/spi_write() with one side NULL, spi_transfer_dma() (lengths,
 * a run longer than 8 KB, unaligned buffers, one side NULL, speed against
 * the FIFO copy), and the CS pin control.
 * LED1 lights if everything passed, LED2 if anything failed.
 */
#include <stdio.h>
#include <string.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_utils.h"
#include "w80x_gpio.h"
#include "w80x_uart.h"
#include "w80x_spi.h"
#include "w80x_dma.h"
#include "boards/hlk_w801_kit.h"

#define PIN_SCK   PB2
#define PIN_MISO  PB3
#define PIN_MOSI  PA7
#define PIN_CS    PB4
#define PIN_CS_SENSE PB7   /* optional wire from PIN_CS, to check spi_cs() */
#define SENSE_US  20       /* settle time before reading the sense pin */

#define DMA_TX_CH 0u
#define DMA_RX_CH 1u
#define BUF_LEN   10000u   /* more than SPI_DMA_MAX_LEN, for a two-run DMA transfer */

static int fails;
static uint8_t tx[BUF_LEN] __attribute__((aligned(4)));
static uint8_t rx[BUF_LEN] __attribute__((aligned(4)));

static void check_ok(const char *name, int ok)
{
    printf("%-34s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) {
        fails++;
    }
}

static void fill(uint32_t len, uint32_t seed)
{
    for (uint32_t i = 0; i < len; i++) {
        tx[i] = (uint8_t)((i * 7u) + seed);
    }
}

/* Loopback len bytes from tx + off into rx + off, by FIFO copy or DMA;
 * 1 if they all came back. Prints the first mismatch. */
static int loop_at(uint32_t len, uint32_t seed, uint32_t off, int dma)
{
    fill(off + len, seed);
    memset(rx, 0, off + len);
    int r = dma ? spi_transfer_dma(DMA_TX_CH, DMA_RX_CH, tx + off, rx + off, len)
                : spi_transfer(tx + off, rx + off, len);
    if (r) {
        printf("    timeout\n");
        return 0;
    }
    for (uint32_t i = off; i < (off + len); i++) {
        if (rx[i] != tx[i]) {
            printf("    byte %lu: sent %02x, got %02x\n",
                   (unsigned long)(i - off), tx[i], rx[i]);
            return 0;
        }
    }
    return 1;
}

static int loop(uint32_t len, uint32_t seed) { return loop_at(len, seed, 0, 0); }

int main(void)
{
    char name[48];

    clock_set_cpu_div(CLK_CPU_240MHZ);
    led_init(LED1);
    led_init(LED2);
    uart0_init(115200);
    stdio_set_uart(UART0);

    printf("\n\nSPI loopback test on the %s, CPU %lu MHz, APB %lu MHz\n",
           BOARD_NAME, (unsigned long)(clock_cpu_hz() / 1000000u),
           (unsigned long)(clock_apb_hz() / 1000000u));
    printf("Needs a wire from MOSI (PA7) to MISO (PB3); optional: CS (PB4) to PB7.\n\n");

    spi_pins(PIN_SCK, PIN_MISO, PIN_MOSI, GPIO_AF2);
    uint32_t hz = spi_init(1000000, SPI_MODE0);
    printf("SCK %lu Hz\n", (unsigned long)hz);

    /* is the wire there? */
    uint8_t a = spi_xfer(0xA5), b = spi_xfer(0x5A);
    if ((a != 0xA5) || (b != 0x5A)) {
        printf("  sent a5 5a, got %02x %02x: is PA7 wired to PB3?\n", a, b);
    }
    check_ok("loopback wire", (a == 0xA5) && (b == 0x5A));

    /* lengths around word and FIFO boundaries */
    static const uint16_t lens[] = { 1, 2, 3, 4, 5, 7, 31, 32, 33, 63, 64, 65, 100, 1000 };
    int ok = 1;
    for (uint32_t i = 0; i < (sizeof lens / sizeof lens[0]); i++) {
        if (!loop(lens[i], i)) {
            printf("    (length %u)\n", lens[i]);
            ok = 0;
        }
    }
    check_ok("lengths 1..1000", ok);

    /* odd length followed by another transfer: no stale FIFO bytes */
    ok = loop(3, 1) && loop(8, 2) && loop(5, 3) && loop(4, 4);
    check_ok("odd length, then next transfer", ok);

    /* all clock modes */
    static const uint32_t modes[] = { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 };
    for (uint32_t m = 0; m < 4; m++) {
        spi_set_mode(modes[m]);
        snprintf(name, sizeof name, "mode %lu", (unsigned long)m);
        check_ok(name, loop(256, m));
    }
    spi_set_mode(SPI_MODE0);

    /* one side NULL */
    memset(rx, 0, 64);
    ok = (spi_read(rx, 64) == 0);
    for (uint32_t i = 0; i < 64; i++) {
        ok &= (rx[i] == 0xFF);
    }
    check_ok("spi_read sends 0xFF", ok);
    fill(64, 9);
    check_ok("spi_write", spi_write(tx, 64) == 0);

    /* clock rates and throughput */
    static const uint32_t rates[] = { 100000, 1000000, 5000000, 10000000, 20000000 };
    for (uint32_t i = 0; i < (sizeof rates / sizeof rates[0]); i++) {
        uint32_t len = (rates[i] < 1000000) ? 256 : 2048;
        hz = spi_set_clock(rates[i]);
        uint32_t t0 = CPU_TICKS;
        int r = loop(len, i);
        uint32_t cyc = cpu_cycles_since(t0);
        uint32_t us = cpu_cycles_to_us(cyc);
        snprintf(name, sizeof name, "SCK %lu kHz, %lu bytes", (unsigned long)(hz / 1000),
                 (unsigned long)len);
        check_ok(name, r);
        if (us) {
            printf("    %lu us, %lu KB/s (wire limit %lu KB/s)\n", (unsigned long)us,
                   (unsigned long)(len * 1000u / us), (unsigned long)(hz / 8000u));
        }
        /* fixed cost of one chunk: 100 single-byte transfers */
        t0 = CPU_TICKS;
        for (uint32_t k = 0; k < 100; k++) {
            spi_xfer((uint8_t)k);
        }
        printf("    1-byte transfer: %lu cycles\n", (unsigned long)(cpu_cycles_since(t0) / 100u));
    }
    spi_set_clock(1000000);

    /* DMA */
    dma_init();
    static const uint16_t dlens[] = { 4, 8, 32, 36, 64, 512, 2048, 1, 3, 5, 513, 2047 };
    ok = 1;
    for (uint32_t i = 0; i < (sizeof dlens / sizeof dlens[0]); i++) {
        if (!loop_at(dlens[i], i, 0, 1)) {
            printf("    (length %u)\n", dlens[i]);
            ok = 0;
        }
    }
    check_ok("DMA lengths 1..2048", ok);
    check_ok("DMA 10000 bytes (two runs)", loop_at(BUF_LEN, 7, 0, 1));
    check_ok("DMA unaligned (FIFO fallback)", loop_at(100, 8, 1, 1) && loop_at(100, 9, 2, 1));
    check_ok("DMA after FIFO, FIFO after DMA", loop(5, 1) && loop_at(64, 2, 0, 1) && loop(7, 3));

    memset(rx, 0, 512);
    ok = (spi_transfer_dma(DMA_TX_CH, DMA_RX_CH, NULL, rx, 512) == 0);
    for (uint32_t i = 0; i < 512; i++) {
        ok &= (rx[i] == 0xFF);
    }
    check_ok("DMA read sends 0xFF", ok);
    fill(512, 11);
    check_ok("DMA write", (spi_transfer_dma(DMA_TX_CH, DMA_RX_CH, tx, NULL, 512) == 0) && loop(16, 12));

    /* speed: FIFO copy vs DMA, 512-byte blocks (an SD sector) and 8 KB */
    hz = spi_set_clock(20000000);
    printf("SCK %lu kHz, FIFO copy vs DMA:\n", (unsigned long)(hz / 1000u));
    static const uint32_t slens[] = { 512, 8188 };
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t t0 = CPU_TICKS;
        int r1 = spi_transfer(tx, rx, slens[i]);
        uint32_t us_fifo = cpu_cycles_to_us(cpu_cycles_since(t0));
        t0 = CPU_TICKS;
        int r2 = spi_transfer_dma(DMA_TX_CH, DMA_RX_CH, tx, rx, slens[i]);
        uint32_t us_dma = cpu_cycles_to_us(cpu_cycles_since(t0));
        printf("    %4lu bytes: FIFO %lu us (%lu KB/s), DMA %lu us (%lu KB/s), wire limit %lu KB/s\n",
               (unsigned long)slens[i], (unsigned long)us_fifo,
               (unsigned long)(us_fifo ? (slens[i] * 1000u / us_fifo) : 0u),
               (unsigned long)us_dma, (unsigned long)(us_dma ? (slens[i] * 1000u / us_dma) : 0u),
               (unsigned long)(hz / 8000u));
        snprintf(name, sizeof name, "DMA faster, %lu bytes", (unsigned long)slens[i]);
        check_ok(name, (r1 == 0) && (r2 == 0) && (us_dma < us_fifo));
    }
    spi_set_clock(1000000);

    /* spi_cs(): needs a second wire from CS (PB4) to PIN_CS_SENSE, since
     * gpio_read() on PB4 itself returns 0 while the SPI drives it. First
     * check the wire with PB4 as a plain GPIO; the sense pin has a
     * pull-down, so a missing wire reads 0 both times. */
    gpio_mode(PIN_CS_SENSE, GPIO_INPUT_PULLDOWN);
    gpio_output(PIN_CS, GPIO_HIGH);
    delay_us(SENSE_US);
    int w_hi = gpio_read(PIN_CS_SENSE);
    gpio_clear(PIN_CS);
    delay_us(SENSE_US);
    int w_lo = gpio_read(PIN_CS_SENSE);
    if ((w_hi != GPIO_HIGH) || (w_lo != GPIO_LOW)) {
        printf("spi_cs() drives PB4                SKIP (no wire PB4 -> PB7)\n");
    } else {
        gpio_af(PIN_CS, GPIO_AF1);
        spi_init(1000000, SPI_MODE0);
        delay_us(SENSE_US);
        int idle = gpio_read(PIN_CS_SENSE);         /* spi_init(): inactive, high */
        spi_cs(1);
        delay_us(SENSE_US);
        int act = gpio_read(PIN_CS_SENSE);          /* active, low */
        int lp = loop(100, 5);                      /* stays low across chunks */
        int act2 = gpio_read(PIN_CS_SENSE);
        spi_cs(0);
        delay_us(SENSE_US);
        int off = gpio_read(PIN_CS_SENSE);          /* inactive again */
        ok = (idle == GPIO_HIGH) && (act == GPIO_LOW) && lp &&
             (act2 == GPIO_LOW) && (off == GPIO_HIGH);
        if (!ok) {
            printf("    after init %d, cs(1) %d, after transfer %d, cs(0) %d (want 1 0 0 1)%s\n",
                   idle, act, act2, off, lp ? "" : ", transfer failed");
        }
        check_ok("spi_cs() drives PB4", ok);
    }
    gpio_mode(PIN_CS, GPIO_INPUT);
    gpio_mode(PIN_CS_SENSE, GPIO_INPUT);

    printf("\n%d failed\n", fails);
    led_on(fails ? LED2 : LED1);
    for (;;) {
        __wait();
    }
}
