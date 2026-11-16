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
 * spi_read()/spi_write() with one side NULL, and the CS pin control.
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
#include "boards/hlk_w801_kit.h"

#define PIN_SCK   PB2
#define PIN_MISO  PB3
#define PIN_MOSI  PA7
#define PIN_CS    PB4
#define PIN_CS_SENSE PB7   /* optional wire from PIN_CS, to check spi_cs() */
#define SENSE_US  20       /* settle time before reading the sense pin */

static int fails;
static uint8_t tx[2048], rx[2048];

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

/* Loopback len bytes; 1 if they all came back. Prints the first mismatch. */
static int loop(uint32_t len, uint32_t seed)
{
    fill(len, seed);
    memset(rx, 0, len);
    if (spi_transfer(tx, rx, len)) {
        printf("    timeout\n");
        return 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (rx[i] != tx[i]) {
            printf("    byte %lu: sent %02x, got %02x\n",
                   (unsigned long)i, tx[i], rx[i]);
            return 0;
        }
    }
    return 1;
}

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
        uint32_t len = (rates[i] < 1000000) ? 256 : sizeof tx;
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
