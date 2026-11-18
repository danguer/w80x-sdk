/*
 * w80x_sdh.h - SDIO host (SD/MMC bus controller), polled. Header-only.
 *
 * This is the bus layer: clock, commands, responses, data blocks. The SD
 * card protocol on top of it (init sequence, addressing, retries) is in
 * examples/lib/sdcard/sdcard_sdio.c.
 *
 *   sdh_pins(PA9, PA10, PA11, PA12, PA13, PA14, GPIO_AF1);   // CLK CMD D0..D3
 *   sdh_init();                          // 1-bit bus, about 300-400 kHz
 *   uint32_t st = sdh_cmd(8, 0x1AA, SDH_IO_CMD_SHORT);
 *   if (!(st & SDH_ERR_MASK)) { uint32_t r7 = sdh_resp(); ... }
 *   sdh_set_clock(20000000);
 *   sdh_read_block(buf, 512);            // after CMD17
 *
 * Pins (W801/W806, wm_iot_sdk): CLK PA9, CMD PA10, DAT0..3 PA11..PA14, all
 * AF1. The W800 uses PB6..PB11 with AF2. CMD and DAT need pull-ups;
 * sdh_pins() turns on the internal ones.
 *
 * Clock: hclk (the CPU clock) / (2 * (sel + 1)), sel 0..7, in high-speed
 * mode, or a ~1 MHz time base / (2 * (sel + 1)) in low-speed mode (manual
 * table 2). sdh_set_clock() picks the fastest rate not above the request.
 * At 240 MHz that is 120, 60, 40, 30, 24, 20, 17.1, 15 MHz, or 937 kHz and
 * below. Call it again after changing the CPU clock.
 *
 * Commands: sdh_cmd() loads the command buffer and writes IO, which starts
 * it (manual table 3): SDH_IO_CMD_NORESP (CMD0), SDH_IO_CMD_SHORT (48-bit
 * response) or SDH_IO_CMD_LONG (136-bit: CID, CSD). It returns the INT_SRC
 * flags, plus SDH_STATUS_STUCK if nothing finished in time.
 *
 * Data: one block through the 512-byte buffer with the CPU
 * (sdh_read_block / sdh_write_block), or several blocks by DMA
 * (sdh_read_blocks_dma / sdh_write_blocks_dma), both after the read or
 * write command. The sequences and register values follow wm_sdk_w80x's
 * wm_sdio_host.c; the multi-block ones only work with DMA there too.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_SDH_H
#define W80X_SDH_H

#include <stddef.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "w80x_dma.h"
#include "w80x_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SDH_CMD_TIMEOUT_MS
#define SDH_CMD_TIMEOUT_MS   100u    /* a command and its response */
#endif
#ifndef SDH_DATA_TIMEOUT_MS
#define SDH_DATA_TIMEOUT_MS  3000u   /* a data transfer, busy time included */
#endif

/* Timeout units for MBCTL: 10 ms, so TIMEOUTCNT 255 allows 2.55 s (less if
 * hclk is above 128 MHz, where the time base runs faster than 1 MHz). */
#define SDH_MBCTL_UNITS      ((2u << SDH_MBCTL_NAC_UNIT_Pos) | (2u << SDH_MBCTL_BUSY_UNIT_Pos))

/* Not a hardware flag: set in a status when the operation never finished. */
#define SDH_STATUS_STUCK     BIT(31)
/* Flags that mean a command or transfer failed. */
#define SDH_ERR_MASK         (SDH_INT_DATA_CRC | SDH_INT_DATA_TIMEOUT | SDH_INT_NCR_TIMEOUT | \
                              SDH_INT_CRC_TOKEN | SDH_STATUS_STUCK)

/* Largest DMA run: DMA_MAX_LEN bytes in whole blocks. */
#define SDH_DMA_MAX_BLOCKS   (DMA_MAX_LEN / SDH_BLOCK_SIZE)

/* Route the pins. Pass GPIO_NO_PIN for d1..d3 on a 1-bit bus. CMD and DAT
 * get the internal pull-up. */
W80X_INLINE void sdh_pins(uint8_t clk, uint8_t cmd, uint8_t d0, uint8_t d1, uint8_t d2,
                          uint8_t d3, gpio_af_t af)
{
    const uint8_t pulled[] = { cmd, d0, d1, d2, d3 };

    for (uint32_t i = 0; i < sizeof pulled; i++) {
        if (pulled[i] != GPIO_NO_PIN) {
            gpio_mode(pulled[i], GPIO_INPUT | GPIO_PULLUP);
            gpio_af(pulled[i], af);
        }
    }
    gpio_af(clk, af);
}

/* Time base for low-speed mode: hclk / ((n + 1) * 2) close to 1 MHz. Six
 * bits, so above 128 MHz it runs faster (240 MHz: 1.875 MHz). */
W80X_INLINE uint32_t sdh__timebase(uint32_t hclk)
{
    uint32_t n = hclk / 2000000u;

    n = n ? (n - 1u) : 0u;
    return (n > SDH_CARDSEL_TIMEBASE_Msk) ? SDH_CARDSEL_TIMEBASE_Msk : n;
}

/* Set the bus clock to at most hz. Returns the rate set. */
static inline uint32_t sdh_set_clock(uint32_t hz)
{
    uint32_t hclk = clock_cpu_hz();
    uint32_t timebase = sdh__timebase(hclk);
    uint32_t slow_base = hclk / ((timebase + 1u) * 2u);
    uint32_t ctrl = SDH->CTRL & ~(SDH_CTRL_CLKSEL_Msk | SDH_CTRL_HIGH_SPEED);
    uint32_t sel = 0;
    uint32_t rate = 0;

    SDH->CARDSEL = SDH_CARDSEL_ENABLE | SDH_CARDSEL_CARD_CLK | timebase;
    while ((sel < 7u) && ((hclk / (2u * (sel + 1u))) > hz)) {
        sel++;
    }
    rate = hclk / (2u * (sel + 1u));
    if (rate <= hz) {
        ctrl |= SDH_CTRL_HIGH_SPEED;
    } else {
        sel = 0;
        while ((sel < 7u) && ((slow_base / (2u * (sel + 1u))) > hz)) {
            sel++;
        }
        rate = slow_base / (2u * (sel + 1u));
    }
    SDH->CTRL = ctrl | (sel << SDH_CTRL_CLKSEL_Pos);
    return rate;
}

/* Data bus widths for sdh_set_width(), as the number of DAT lines. */
#define SDH_WIDTH_1BIT       1u      /* DAT0 only (the width after reset) */
#define SDH_WIDTH_4BIT       4u      /* DAT0..DAT3 */

/* SDH_WIDTH_1BIT or SDH_WIDTH_4BIT (tell the card first, with ACMD6). */
W80X_INLINE void sdh_set_width(uint32_t width)
{
    if (width == SDH_WIDTH_4BIT) {
        SDH->CTRL |= SDH_CTRL_WIDTH4;
    } else {
        SDH->CTRL &= ~SDH_CTRL_WIDTH4;
    }
}

/* Clock on, out of reset, SD memory mode, push-pull, 1-bit bus, CRCs on,
 * interrupts off (the flags are polled), clock about 400 kHz. */
static inline void sdh_init(void)
{
    clock_enable(CLK_GATE_SDIO_M);
    periph_reset(CLK_RST_SDIO_M);

    SDH->CTRL = SDH_CTRL_MMC | SDH_CTRL_AUTO_SIG;
    SDH->INT_MASK = 0;
    SDH->INT_SRC = SDH_INT_ALL;
    SDH->CRCCTL = SDH_CRCCTL_CMD_EN | SDH_CRCCTL_DAT_EN;
    SDH->TIMEOUTCNT = 0xFFu;
    SDH->MBCTL = SDH_MBCTL_UNITS;
    SDH->BUF_CTL = SDH_BUF_DMA_REQ_MASK | SDH_BUF_FLUSH;
    sdh_set_clock(400000u);
}

/* Wait while (reg & mask) is nonzero, up to ms. 1 = it cleared. */
static inline int sdh__wait_clear(__IO uint32_t *reg, uint32_t mask, uint32_t ms)
{
    uint32_t last = CPU_TICKS;
    uint32_t elapsed = 0;
    uint32_t limit = cpu_ms_to_cycles(ms);

    while (*reg & mask) {
        elapsed += cpu_cycles_step(&last);
        if (elapsed >= limit) {
            return 0;
        }
    }
    return 1;
}

/* Send a command (index 0..63) and, per io, receive its response. Returns
 * the INT_SRC flags; check them against SDH_ERR_MASK and SDH_INT_CMD_CRC
 * (which R2 and R3 responses always set). */
static inline uint32_t sdh_cmd(uint8_t cmd, uint32_t arg, uint32_t io)
{
    SDH->CMD_BUF[4] = 0x40u | (cmd & 0x3Fu);
    SDH->CMD_BUF[3] = (arg >> 24) & 0xFFu;
    SDH->CMD_BUF[2] = (arg >> 16) & 0xFFu;
    SDH->CMD_BUF[1] = (arg >> 8) & 0xFFu;
    SDH->CMD_BUF[0] = arg & 0xFFu;
    SDH->INT_SRC = SDH_INT_ALL;
    SDH->IO = io;

    /* IO[2] clears itself when the command (and response) are done */
    int done = sdh__wait_clear(&SDH->IO, SDH_IO_CMD_START, SDH_CMD_TIMEOUT_MS);
    uint32_t status = SDH->INT_SRC;
    SDH->INT_SRC = status;
    if (!done || !(status & (SDH_INT_CMD_DONE | SDH_INT_NCR_TIMEOUT))) {
        status |= SDH_STATUS_STUCK;
    }
    return status;
}

/* 48-bit response: its 32-bit argument (card status, OCR, RCA, R7). */
W80X_INLINE uint32_t sdh_resp(void)
{
    return (SDH->CMD_BUF[0] & 0xFFu) | ((SDH->CMD_BUF[1] & 0xFFu) << 8) |
           ((SDH->CMD_BUF[2] & 0xFFu) << 16) | ((SDH->CMD_BUF[3] & 0xFFu) << 24);
}

/* 48-bit response: the command index it carries (0x3F for R2 and R3). */
W80X_INLINE uint32_t sdh_resp_index(void)
{
    return SDH->CMD_BUF[4] & 0x3Fu;
}

/* 136-bit response (CID or CSD) as the card sends it, MSB first. The
 * buffer holds bits 127..8; the last byte (CRC7 and end bit) is not kept
 * and is set to 0. */
W80X_INLINE void sdh_resp_long(uint8_t out[16])
{
    for (uint32_t i = 0; i < 15u; i++) {
        out[i] = (uint8_t)SDH->CMD_BUF[14u - i];
    }
    out[15] = 0;
}

/* 8 clocks with CMD high (e.g. 10 times for the 74 power-up clocks). */
W80X_INLINE void sdh_clocks(void)
{
    SDH->INT_SRC = SDH_INT_ALL;
    SDH->IO = SDH_IO_8_CLOCKS;
    sdh__wait_clear(&SDH->IO, SDH_IO_CMD_START, SDH_CMD_TIMEOUT_MS);
}

/* Receive one data block of len bytes (a multiple of 4, at most 512) after
 * the command that asked for it (CMD17, ACMD13, ...). Returns the INT_SRC
 * flags, SDH_STATUS_STUCK added on a timeout. buf needs no alignment. */
static inline uint32_t sdh_read_block(void *buf, uint32_t len)
{
    uint8_t *pos = (uint8_t *)buf;

    SDH->BUF_CTL = SDH_BUF_FLUSH | SDH_BUF_DMA_REQ_MASK | SDH_BUF_LEVEL(8);
    SDH->BYTECNT = len;
    SDH->INT_SRC = SDH_INT_ALL;
    SDH->IO = SDH_IO_DATA_READ | SDH_IO_DATA_START;

    int done = sdh__wait_clear(&SDH->IO, SDH_IO_DATA_START, SDH_DATA_TIMEOUT_MS);
    uint32_t status = SDH->INT_SRC;
    SDH->INT_SRC = status;
    if (!done || !(status & SDH_INT_DATA_DONE)) {
        return status | SDH_STATUS_STUCK;
    }
    for (uint32_t i = 0; i < len; i += 4u) {
        uint32_t word = SDH->DATA_BUF[0];        /* the buffer reads as a FIFO */
        pos[i]      = (uint8_t)word;
        pos[i + 1u] = (uint8_t)(word >> 8);
        pos[i + 2u] = (uint8_t)(word >> 16);
        pos[i + 3u] = (uint8_t)(word >> 24);
    }
    return status;
}

/* Send one data block of len bytes after a write command (CMD24). Returns
 * when the block is sent and the card's CRC token is in; the card may then
 * still be busy programming (poll CMD13). */
static inline uint32_t sdh_write_block(const void *buf, uint32_t len)
{
    const uint8_t *pos = (const uint8_t *)buf;

    SDH->BUF_CTL = SDH_BUF_FLUSH | SDH_BUF_DMA_REQ_MASK | SDH_BUF_WRITE | SDH_BUF_LEVEL(8);
    for (uint32_t i = 0; i < len; i += 4u) {
        SDH->DATA_BUF[i / 4u] = (uint32_t)pos[i] | ((uint32_t)pos[i + 1u] << 8) |
                                ((uint32_t)pos[i + 2u] << 16) | ((uint32_t)pos[i + 3u] << 24);
    }
    SDH->BYTECNT = len;
    SDH->INT_SRC = SDH_INT_ALL;
    SDH->IO = SDH_IO_DATA_START;

    int done = sdh__wait_clear(&SDH->IO, SDH_IO_DATA_START, SDH_DATA_TIMEOUT_MS);
    uint32_t status = SDH->INT_SRC;
    SDH->INT_SRC = status;
    if (!done || !(status & SDH_INT_DATA_DONE)) {
        status |= SDH_STATUS_STUCK;
    }
    return status;
}

/* DMA can use buf: word-aligned, in SRAM. */
W80X_INLINE int sdh_dma_ok(const void *buf)
{
    uintptr_t addr = (uintptr_t)buf;

    return ((addr & 3u) == 0u) && (addr >= SRAM_BASE) && (addr < (SRAM_BASE + SRAM_SIZE));
}

/* Multi-block data by DMA channel ch, after CMD18 / CMD25 (then send CMD12).
 * blocks <= SDH_DMA_MAX_BLOCKS, buf as sdh_dma_ok(). dma_init() first. */
static inline uint32_t sdh__blocks_dma(uint32_t ch, void *buf, uint32_t blocks, int read)
{
    uint32_t len = blocks * SDH_BLOCK_SIZE;

    SDH->BUF_CTL = SDH_BUF_FLUSH | SDH_BUF_DMA_REQ_MASK;
    dma_req_clear(DMA_REQ_SDIO_HOST);
    if (read) {
        dma_setup(ch, buf, &SDH->DATA_BUF[0], len, DMA_SRC_FIXED | DMA_DST_INC | DMA_WIDTH_32,
                  DMA_REQ_SDIO_HOST);
    } else {
        dma_setup(ch, &SDH->DATA_BUF[0], buf, len, DMA_SRC_INC | DMA_DST_FIXED | DMA_WIDTH_32,
                  DMA_REQ_SDIO_HOST);
    }
    dma_start(ch);                              /* the DMA goes first (manual 13.4 note 1) */
    SDH->BUF_CTL = read ? (SDH_BUF_DMA | SDH_BUF_LEVEL(1))
                        : (SDH_BUF_DMA | SDH_BUF_WRITE | SDH_BUF_LEVEL(8));
    SDH->BLOCKCNT = blocks;
    SDH->INT_SRC = SDH_INT_ALL;
    SDH->MBCTL = SDH_MBCTL_UNITS | (read ? SDH_MBCTL_READ : 0u) | SDH_MBCTL_START;

    int done = sdh__wait_clear(&SDH->MBCTL, SDH_MBCTL_START, SDH_DATA_TIMEOUT_MS);
    if (done && read) {
        done = sdh__wait_clear(&dma_ch(ch)->CHCTRL, DMA_CHCTRL_ON, SDH_CMD_TIMEOUT_MS);
    }
    uint32_t status = SDH->INT_SRC;
    SDH->INT_SRC = status;
    if (!done) {
        status |= SDH_STATUS_STUCK;
        SDH->MBCTL = SDH_MBCTL_UNITS;
    }
    dma_stop(ch);
    SDH->BUF_CTL = SDH_BUF_DMA_REQ_MASK;
    return status;
}

W80X_INLINE uint32_t sdh_read_blocks_dma(uint32_t ch, void *buf, uint32_t blocks)
{
    return sdh__blocks_dma(ch, buf, blocks, 1);
}

W80X_INLINE uint32_t sdh_write_blocks_dma(uint32_t ch, const void *buf, uint32_t blocks)
{
    return sdh__blocks_dma(ch, (void *)buf, blocks, 0);
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_SDH_H */
