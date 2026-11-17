/*
 * w80x_spi.h - SPI master (LSPI, manual ch. 14), polled. Header-only.
 *
 *   spi_pins(PB2, PB3, PA7, GPIO_AF2);       // SCK, MISO, MOSI
 *   gpio_output(PB4, GPIO_HIGH);             // chip select as a GPIO
 *   spi_init(1000000, SPI_MODE0);            // 1 MHz, CPOL 0 / CPHA 0
 *
 *   gpio_clear(PB4);                         // select the device
 *   spi_write(cmd, 4);
 *   spi_read(buf, 256);                      // clocks out SPI_DUMMY_BYTE (0xFF)
 *   gpio_set(PB4);
 *
 * Full duplex: spi_transfer(tx, rx, len); either buffer may be NULL (sends
 * SPI_DUMMY_BYTE / discards what comes back). spi_xfer(byte) for single
 * bytes. Bytes go out MSB first, in buffer order.
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
 * DMA: spi_transfer_dma(tx_ch, rx_ch, tx, rx, len) moves the data with two
 * DMA channels (call dma_init() once first), in one run of up to 8 KB with
 * no pauses and no CPU copying. It needs word-aligned SRAM buffers: for
 * anything else (unaligned, in flash, or the last len % 4 bytes) it falls
 * back to spi_transfer(). It still waits until the transfer is done. The
 * sequence follows wm_iot_sdk (wm_hal_spim_tx_rx_dma_sync).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_SPI_H
#define W80X_SPI_H

#include <stddef.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "w80x_dma.h"

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
/* Byte sent while only receiving (tx NULL). 0xFF keeps MOSI high, which SD
 * cards and most flash chips expect. */
#define SPI_DUMMY_BYTE 0xFFu

#ifndef SPI_MAX_EMPTY_FIFO_TRIES
#define SPI_MAX_EMPTY_FIFO_TRIES 100000u   /* poll loops for CLEAR_FIFO to self-clear */
#endif

/* Route SCK, MISO and MOSI. Pass GPIO_NO_PIN to skip one (e.g. no MISO). */
W80X_INLINE void spi_pins(uint8_t sck, uint8_t miso, uint8_t mosi, gpio_af_t af)
{
    if (sck != GPIO_NO_PIN) {
        gpio_af(sck, af);
    }
    if (miso != GPIO_NO_PIN) {
        gpio_af(miso, af);
    }
    if (mosi != GPIO_NO_PIN) {
        gpio_af(mosi, af);
    }
}

/* Largest divide factor: CLK_CFG's Divider field is 16 bits (manual 14.4.4)
 * and SCK = APB / (2 * (Divider + 1)), so div = Divider + 1 <= 0x10000.
 * At APB 40 MHz that is about 305 Hz. */
#define SPI_CLK_DIV_MAX 0x10000u

/* Set SCK to at most hz. Returns the rate set. */
static inline uint32_t spi_set_clock(uint32_t hz)
{
    uint32_t apb = clock_apb_hz();
    uint32_t div = hz ? (apb + (2u * hz) - 1u) / (2u * hz) : SPI_CLK_DIV_MAX; /* ceil */
    if (div > SPI_CLK_DIV_MAX) {
        div = SPI_CLK_DIV_MAX;
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
    uint32_t ch_cfg = SPI->CH_CFG & ~(SPI_CH_START | SPI_CH_CS_LEVEL | SPI_CH_CLEAR_FIFO);
    SPI->CH_CFG = ch_cfg | SPI_CH_CS_FORCE | (active ? 0 : SPI_CH_CS_LEVEL);
}

/* Master, Motorola format, polled, interrupts masked. Returns the SCK rate. */
static inline uint32_t spi_init(uint32_t hz, uint32_t mode)
{
    clock_enable(CLK_GATE_LSPI);
    periph_reset(CLK_RST_LSPI);

    SPI->CH_CFG = SPI_CH_CLEAR_FIFO | SPI_CH_CS_FORCE | SPI_CH_CS_LEVEL;
    for (uint32_t tries = SPI_MAX_EMPTY_FIFO_TRIES; (SPI->CH_CFG & SPI_CH_CLEAR_FIFO) && --tries; ) {
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

/* One chunk of len <= SPI_FIFO_SIZE bytes. */
static inline int spi__chunk(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    uint32_t cs = SPI->CH_CFG & (SPI_CH_CS_FORCE | SPI_CH_CS_LEVEL);
    uint32_t ch_cfg = cs | SPI_CH_TX_ON | SPI_CH_RX_ON | SPI_CH_CONTINUE;

    /* Empty both FIFOs first: the TX FIFO takes whole words, so after a
     * length that is not a multiple of 4 the unused bytes of the last word
     * are still in it. */
    SPI->CH_CFG = cs | SPI_CH_CLEAR_FIFO;
    for (uint32_t tries = SPI_MAX_EMPTY_FIFO_TRIES; (SPI->CH_CFG & SPI_CH_CLEAR_FIFO) && --tries; ) {
    }
    SPI->CH_CFG = ch_cfg;
    for (uint32_t pos = 0; pos < len; pos += 4) {       /* first byte = low byte */
        uint32_t word = 0;
        for (uint32_t byte = 0; (byte < 4) && ((pos + byte) < len); byte++) {
            word |= (uint32_t)(tx ? tx[pos + byte] : SPI_DUMMY_BYTE) << (8u * byte);
        }
        SPI->TXDATA = word;
    }
    SPI->CH_CFG = ch_cfg | SPI_CH_LEN(8u * len) | SPI_CH_START;

    uint32_t tries = SPI_TIMEOUT;
    while (((SPI->CH_CFG & SPI_CH_START) || (SPI->STATUS & SPI_STATUS_BUSY) ||
            (((SPI->STATUS & SPI_STATUS_RX_LEVEL_Msk) >> SPI_STATUS_RX_LEVEL_Pos) < len)) &&
           --tries) {
    }
    if (!tries) {
        SPI->CH_CFG = cs | SPI_CH_CLEAR_FIFO;
        return -1;
    }

    for (uint32_t pos = 0; pos < len; pos += 4) {
        uint32_t word = SPI->RXDATA;
        for (uint32_t byte = 0; (byte < 4) && ((pos + byte) < len); byte++) {
            if (rx) {
                rx[pos + byte] = (uint8_t)(word >> (8u * byte));
            }
        }
    }
    SPI->INT_SRC = SPI_INT_ALL;
    return 0;
}

/* Full duplex: send len bytes from tx while receiving len bytes into rx.
 * tx NULL sends SPI_DUMMY_BYTE, rx NULL discards. Returns 0, or -1 on a timeout. */
static inline int spi_transfer(const void *tx, void *rx, size_t len)
{
    const uint8_t *tx_pos = (const uint8_t *)tx;
    uint8_t *rx_pos = (uint8_t *)rx;

    while (len) {
        uint32_t chunk_len = (len < SPI_FIFO_SIZE) ? (uint32_t)len : SPI_FIFO_SIZE;
        if (spi__chunk(tx_pos, rx_pos, chunk_len)) {
            return -1;
        }
        if (tx_pos) {
            tx_pos += chunk_len;
        }
        if (rx_pos) {
            rx_pos += chunk_len;
        }
        len -= chunk_len;
    }
    return 0;
}

W80X_INLINE int spi_write(const void *tx, size_t len) { return spi_transfer(tx, NULL, len); }
W80X_INLINE int spi_read(void *rx, size_t len)        { return spi_transfer(NULL, rx, len); }

/* Send one byte, return the byte received at the same time. */
static inline uint8_t spi_xfer(uint8_t tx_byte)
{
    uint8_t rx_byte = 0xFF;
    spi_transfer(&tx_byte, &rx_byte, 1);
    return rx_byte;
}

/* ---- DMA ------------------------------------------------------------------ */

/* Longest DMA run: CH_CFG's length field counts bits (16 bits wide), so
 * 8191 bytes, rounded down to whole words. */
#define SPI_DMA_MAX_LEN 8188u

/* DMA can use buf: NULL, or a word-aligned address in SRAM. */
W80X_INLINE int spi__dma_ok(const void *buf)
{
    uintptr_t addr = (uintptr_t)buf;

    return (buf == NULL) ||
           (((addr & 3u) == 0u) && (addr >= SRAM_BASE) && (addr < (SRAM_BASE + SRAM_SIZE)));
}

/* One DMA run of len bytes, len a multiple of 4 and at most SPI_DMA_MAX_LEN. */
static inline int spi__dma_run(uint32_t tx_ch, uint32_t rx_ch, const uint8_t *tx, uint8_t *rx,
                               uint32_t len)
{
    /* Sent when tx is NULL: SPI_DUMMY_BYTE in every byte. Kept in RAM, as the
     * DMA can't read flash. */
    static uint32_t dummy = SPI_DUMMY_BYTE * 0x01010101u;
    uint32_t cs = SPI->CH_CFG & (SPI_CH_CS_FORCE | SPI_CH_CS_LEVEL);
    uint32_t ch_cfg = cs | SPI_CH_TX_ON | SPI_CH_CONTINUE | (rx ? SPI_CH_RX_ON : 0u);

    SPI->CH_CFG = cs | SPI_CH_CLEAR_FIFO;
    for (uint32_t tries = SPI_MAX_EMPTY_FIFO_TRIES; (SPI->CH_CFG & SPI_CH_CLEAR_FIFO) && --tries; ) {
    }
    SPI->CH_CFG = ch_cfg;

    dma_req_clear(DMA_REQ_LSPI_TX);
    dma_req_clear(DMA_REQ_LSPI_RX);
    if (rx) {
        dma_setup(rx_ch, rx, &SPI->RXDATA, len, DMA_SRC_FIXED | DMA_DST_INC | DMA_WIDTH_32,
                  DMA_REQ_LSPI_RX);
    }
    dma_setup(tx_ch, &SPI->TXDATA, tx ? (const void *)tx : (const void *)&dummy, len,
              (tx ? DMA_SRC_INC : DMA_SRC_FIXED) | DMA_DST_FIXED | DMA_WIDTH_32, DMA_REQ_LSPI_TX);
    SPI->MODE_CFG = SPI_MODECFG_TX_DMA | (rx ? SPI_MODECFG_RX_DMA : 0u);   /* trigger levels 0 */
    if (rx) {
        dma_start(rx_ch);
    }
    dma_start(tx_ch);
    SPI->CH_CFG = ch_cfg | SPI_CH_LEN(8u * len) | SPI_CH_START;

    uint32_t tries = SPI_TIMEOUT;
    while ((dma_busy(tx_ch) || (rx && dma_busy(rx_ch)) ||
            (SPI->CH_CFG & SPI_CH_START) || (SPI->STATUS & SPI_STATUS_BUSY)) && --tries) {
    }
    SPI->MODE_CFG = 0;
    if (!tries) {
        dma_stop(tx_ch);
        if (rx) {
            dma_stop(rx_ch);
        }
        SPI->CH_CFG = cs | SPI_CH_CLEAR_FIFO;
        return -1;
    }
    SPI->INT_SRC = SPI_INT_ALL;
    return 0;
}

/* Like spi_transfer(), with the data moved by DMA channels tx_ch and rx_ch
 * (two different channels; rx_ch is unused when rx is NULL). Falls back to
 * spi_transfer() for buffers that are not word-aligned SRAM, and for the
 * last len % 4 bytes. Returns 0, or -1 on a timeout. */
static inline int spi_transfer_dma(uint32_t tx_ch, uint32_t rx_ch, const void *tx, void *rx,
                                   size_t len)
{
    const uint8_t *tx_pos = (const uint8_t *)tx;
    uint8_t *rx_pos = (uint8_t *)rx;

    if (!spi__dma_ok(tx) || !spi__dma_ok(rx)) {
        return spi_transfer(tx, rx, len);
    }
    while (len >= 4u) {
        uint32_t run_len = (len > SPI_DMA_MAX_LEN) ? SPI_DMA_MAX_LEN : ((uint32_t)len & ~3u);
        if (spi__dma_run(tx_ch, rx_ch, tx_pos, rx_pos, run_len)) {
            return -1;
        }
        if (tx_pos) {
            tx_pos += run_len;
        }
        if (rx_pos) {
            rx_pos += run_len;
        }
        len -= run_len;
    }
    return len ? spi_transfer(tx_pos, rx_pos, len) : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_SPI_H */
