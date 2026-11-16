/*
 * w80x_spi.h - SPI master (LSPI, manual ch. 14), polled. Header-only.
 *
 *   spi_pins(PB2, PB3, PA7, GPIO_AF2);       // SCK, MISO, MOSI
 *   gpio_output(PB4, GPIO_HIGH);             // chip select as a GPIO
 *   spi_init(1000000, SPI_MODE0);            // 1 MHz, CPOL 0 / CPHA 0
 *
 *   gpio_clear(PB4);                         // select the device
 *   spi_write(cmd, 4);
 *   spi_read(buf, 256);                      // clocks out 0xFF
 *   gpio_set(PB4);
 *
 * Full duplex: spi_transfer(tx, rx, len); either buffer may be NULL (sends
 * 0xFF / discards what comes back). spi_xfer(byte) for single bytes. Bytes
 * go out MSB first, in buffer order.
 *
 * Chip select: driving a GPIO yourself (above) is simplest and works for any
 * transfer length. The SPI's own CS pin can be used as well: route it with
 * gpio_af() and set its level with spi_cs(1) / spi_cs(0). spi_init() leaves
 * it inactive (high).
 *
 * Pins (wm_sdk_w80x wm_gpio_afsel.c, datasheet pin table):
 *
 *   SCK   PB1 (AF2)  PB2 (AF2)  PB15 (AF3)  PB24 (AF1)
 *   MISO  PB0 (AF2)  PB3 (AF2)  PB16 (AF3)  PB25 (AF1)
 *   MOSI  PA7 (AF2)  PB5 (AF1)  PB17 (AF3)  PB26 (AF1)
 *   CS    PA0 (AF2)  PB4 (AF1)  PB14 (AF3)  PB23 (AF1)
 *
 * On the HLK-W801-KIT, PB5, PB16, PB17, PB25 and PB26 drive LEDs, so
 * SCK PB2 / MISO PB3 / MOSI PA7 (all AF2) is the free set. PA0 is BOOTMODE.
 *
 * Clock: SCK = APB / (2 * (div + 1)), at most APB / 2 = 20 MHz with the
 * usual 40 MHz APB (the manual's master limit). spi_init() and
 * spi_set_clock() return the rate actually set (the nearest one not above
 * the request). Call spi_set_clock() again after changing the CPU clock.
 *
 * How a transfer runs: the hardware FIFOs hold 32 bytes each way, so data
 * is moved in chunks of up to 32 bytes: clear the FIFOs, fill TX, start,
 * wait, empty RX (the same scheme as the wm_iot_sdk polled driver). SCK
 * pauses briefly between chunks; a GPIO chip select stays active.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_SPI_H
#define W80X_SPI_H

#include <stddef.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clock modes: CPOL (idle level) and CPHA (sample edge), as usual. */
#define SPI_MODE0   0u                                  /* idle low, sample rising  */
#define SPI_MODE1   SPI_CFG_CPHA                        /* idle low, sample falling */
#define SPI_MODE2   SPI_CFG_CPOL                        /* idle high, sample falling */
#define SPI_MODE3   (SPI_CFG_CPOL | SPI_CFG_CPHA)       /* idle high, sample rising */

#ifndef SPI_TIMEOUT
#define SPI_TIMEOUT 0x4000000u   /* poll loops per chunk; covers the slowest clock */
#endif

/* Route SCK, MISO and MOSI. Pass 0xFF to skip one (e.g. no MISO). */
W80X_INLINE void spi_pins(uint8_t sck, uint8_t miso, uint8_t mosi, gpio_af_t af)
{
    if (sck != 0xFFu) {
        gpio_af(sck, af);
    }
    if (miso != 0xFFu) {
        gpio_af(miso, af);
    }
    if (mosi != 0xFFu) {
        gpio_af(mosi, af);
    }
}

/* Set SCK to at most hz. Returns the rate set. */
static inline uint32_t spi_set_clock(uint32_t hz)
{
    uint32_t apb = clock_apb_hz();
    uint32_t div = hz ? (apb + (2u * hz) - 1u) / (2u * hz) : 0x10000u; /* ceil */
    if (div > 0x10000u) {
        div = 0x10000u;
    }
    if (div < 1u) {
        div = 1u;
    }
    SPI->CLK_CFG = div - 1u;
    /* MOSI is delayed 1 APB cycle after the SCK edge (wm_iot_sdk's master
     * setting, for hold time). At APB / 2 that is half an SCK period, so the
     * data would change right on the sampling edge: no delay there. */
    SPI->CFG = (SPI->CFG & ~SPI_CFG_OUT_DELAY_Msk) |
               (((div > 1u) ? 1u : 0u) << SPI_CFG_OUT_DELAY_Pos);
    return apb / (2u * div);
}

/* SPI_MODE0..SPI_MODE3 */
W80X_INLINE void spi_set_mode(uint32_t mode)
{
    SPI->CFG = (SPI->CFG & ~(SPI_CFG_CPOL | SPI_CFG_CPHA)) | (mode & (SPI_CFG_CPOL | SPI_CFG_CPHA));
}

/* Drive the SPI's own CS pin (if routed): 1 = active (low), 0 = inactive. */
W80X_INLINE void spi_cs(int active)
{
    uint32_t v = SPI->CH_CFG & ~(SPI_CH_START | SPI_CH_CS_LEVEL | SPI_CH_CLEAR_FIFO);
    SPI->CH_CFG = v | SPI_CH_CS_FORCE | (active ? 0 : SPI_CH_CS_LEVEL);
}

/* Master, Motorola format, polled, interrupts masked. Returns the SCK rate. */
static inline uint32_t spi_init(uint32_t hz, uint32_t mode)
{
    clock_enable(CLK_GATE_LSPI);
    periph_reset(CLK_RST_LSPI);

    SPI->CH_CFG = SPI_CH_CLEAR_FIFO | SPI_CH_CS_FORCE | SPI_CH_CS_LEVEL;
    for (uint32_t t = 100000u; (SPI->CH_CFG & SPI_CH_CLEAR_FIFO) && --t; ) {
    }
    /* CS setup/hold of 1: wm_iot_sdk's master settings. spi_set_clock()
     * sets the MOSI output delay. */
    SPI->CFG = SPI_CFG_MASTER | (mode & (SPI_CFG_CPOL | SPI_CFG_CPHA)) |
               (1u << SPI_CFG_CS_SETUP_Pos) | (1u << SPI_CFG_CS_HOLD_Pos);
    SPI->MODE_CFG = 0;
    SPI->INT_MASK = SPI_INT_ALL;
    SPI->INT_SRC  = SPI_INT_ALL;
    return spi_set_clock(hz);
}

/* One chunk of n <= SPI_FIFO_SIZE bytes. */
static inline int spi__chunk(const uint8_t *tx, uint8_t *rx, uint32_t n)
{
    uint32_t cs = SPI->CH_CFG & (SPI_CH_CS_FORCE | SPI_CH_CS_LEVEL);
    uint32_t ch = cs | SPI_CH_TX_ON | SPI_CH_RX_ON | SPI_CH_CONTINUE;

    /* Empty both FIFOs first: the TX FIFO takes whole words, so after a
     * length that is not a multiple of 4 the unused bytes of the last word
     * are still in it. */
    SPI->CH_CFG = cs | SPI_CH_CLEAR_FIFO;
    for (uint32_t t = 10000u; (SPI->CH_CFG & SPI_CH_CLEAR_FIFO) && --t; ) {
    }
    SPI->CH_CFG = ch;
    for (uint32_t i = 0; i < n; i += 4) {               /* first byte = low byte */
        uint32_t w = 0;
        for (uint32_t k = 0; (k < 4) && ((i + k) < n); k++) {
            w |= (uint32_t)(tx ? tx[i + k] : 0xFFu) << (8u * k);
        }
        SPI->TXDATA = w;
    }
    SPI->CH_CFG = ch | SPI_CH_LEN(8u * n) | SPI_CH_START;

    uint32_t t = SPI_TIMEOUT;
    while (((SPI->CH_CFG & SPI_CH_START) || (SPI->STATUS & SPI_STATUS_BUSY) ||
            (((SPI->STATUS & SPI_STATUS_RX_LEVEL_Msk) >> SPI_STATUS_RX_LEVEL_Pos) < n)) && --t) {
    }
    if (!t) {
        SPI->CH_CFG = cs | SPI_CH_CLEAR_FIFO;
        return -1;
    }

    for (uint32_t i = 0; i < n; i += 4) {
        uint32_t w = SPI->RXDATA;
        for (uint32_t k = 0; (k < 4) && ((i + k) < n); k++) {
            if (rx) {
                rx[i + k] = (uint8_t)(w >> (8u * k));
            }
        }
    }
    SPI->INT_SRC = SPI_INT_ALL;
    return 0;
}

/* Full duplex: send len bytes from tx while receiving len bytes into rx.
 * tx NULL sends 0xFF, rx NULL discards. Returns 0, or -1 on a timeout. */
static inline int spi_transfer(const void *tx, void *rx, size_t len)
{
    const uint8_t *t = (const uint8_t *)tx;
    uint8_t *r = (uint8_t *)rx;

    while (len) {
        uint32_t n = (len < SPI_FIFO_SIZE) ? (uint32_t)len : SPI_FIFO_SIZE;
        if (spi__chunk(t, r, n)) {
            return -1;
        }
        if (t) {
            t += n;
        }
        if (r) {
            r += n;
        }
        len -= n;
    }
    return 0;
}

W80X_INLINE int spi_write(const void *tx, size_t len) { return spi_transfer(tx, NULL, len); }
W80X_INLINE int spi_read(void *rx, size_t len)        { return spi_transfer(NULL, rx, len); }

/* Send one byte, return the byte received at the same time. */
static inline uint8_t spi_xfer(uint8_t b)
{
    uint8_t r = 0xFF;
    spi_transfer(&b, &r, 1);
    return r;
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_SPI_H */
