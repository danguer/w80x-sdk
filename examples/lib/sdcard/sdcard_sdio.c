/*
 * sdcard_sdio.c - SD card on the SDIO host (SD bus mode, 1 or 4 data
 * lines). Implements sdcard.h; the bus layer is w80x_sdh.h.
 *
 * Wiring (defaults: the W801/W806 SDIO pins, wm_iot_sdk's choice):
 *
 *   card            W80x
 *   CLK             PA9    SDCARD_SDIO_CLK
 *   CMD             PA10   SDCARD_SDIO_CMD   (pull-up)
 *   DAT0            PA11   SDCARD_SDIO_D0    (pull-up)
 *   DAT1            PA12   SDCARD_SDIO_D1    (pull-up; 4-bit bus only)
 *   DAT2            PA13   SDCARD_SDIO_D2    (pull-up; 4-bit bus only)
 *   DAT3 / CD       PA14   SDCARD_SDIO_D3    (pull-up; 4-bit bus only)
 *   CD              none   SDCARD_SDIO_CD    (optional card detect, low = card in)
 *   VDD 3.3 V, GND
 *
 * All on SDCARD_SDIO_AF (default GPIO_AF1). The W800 has the SDIO pins on
 * PB6..PB11 with AF2 instead. The internal pull-ups are turned on; with
 * long wires add 10-47 kOhm external ones to 3.3 V. Breakout boards made
 * for SPI with a level shifter do not work: the shifter drives MISO (DAT0)
 * one way only, and DAT1/DAT2 are not connected.
 *
 * Bus: SDCARD_SDIO_WIDTH 4 (default) or 1. Clock: about 400 kHz while the
 * card initializes, then SDCARD_SDIO_HZ (default 20 MHz; at most 25 MHz,
 * the SD default-speed limit). The rates come from the CPU clock divided by
 * 2..16 (w80x_sdh.h), so 240 MHz gives 24 MHz for 25 MHz, 20 for 20.
 *
 * CRC: the controller generates and checks every CRC itself. Reads and
 * writes are retried (SDCARD_SDIO_RETRIES) after a CRC error or a timeout.
 *
 * DMA: several sectors in one call go as one multi-block transfer by DMA
 * channel SDCARD_SDIO_DMA_CH (default 5), up to 127 blocks at a time, if
 * the buffer is word-aligned SRAM. Single sectors, and buffers DMA cannot
 * use, go one block at a time through the controller's buffer instead.
 * Build with -DSDCARD_SDIO_DMA=0 to always do that.
 *
 * Cards: SD v1, SDSC, SDHC and SDXC. Not MMC (its bus init differs).
 *
 * The sequences follow the SD Physical Layer Simplified Spec. (ch. 4) and
 * wm_sdk_w80x's wm_sdio_host.c, which also gave the register values.
 *
 * Polled, one card. Not reentrant: call from one context only.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "w80x.h"
#include "w80x_gpio.h"
#include "w80x_dma.h"
#include "w80x_sdh.h"
#include "w80x_utils.h"
#include "sdcard.h"

#ifndef SDCARD_SDIO_CLK
#define SDCARD_SDIO_CLK   PA9
#endif
#ifndef SDCARD_SDIO_CMD
#define SDCARD_SDIO_CMD   PA10
#endif
#ifndef SDCARD_SDIO_D0
#define SDCARD_SDIO_D0    PA11
#endif
#ifndef SDCARD_SDIO_D1
#define SDCARD_SDIO_D1    PA12
#endif
#ifndef SDCARD_SDIO_D2
#define SDCARD_SDIO_D2    PA13
#endif
#ifndef SDCARD_SDIO_D3
#define SDCARD_SDIO_D3    PA14
#endif
#ifndef SDCARD_SDIO_AF
#define SDCARD_SDIO_AF    GPIO_AF1
#endif
#ifndef SDCARD_SDIO_CD
#define SDCARD_SDIO_CD    GPIO_NO_PIN
#endif
#ifndef SDCARD_SDIO_WIDTH
#define SDCARD_SDIO_WIDTH 4
#endif
#ifndef SDCARD_SDIO_INIT_HZ
#define SDCARD_SDIO_INIT_HZ 400000u
#endif
#ifndef SDCARD_SDIO_HZ
#define SDCARD_SDIO_HZ    20000000u
#endif
#ifndef SDCARD_SDIO_RETRIES
#define SDCARD_SDIO_RETRIES 3
#endif
#ifndef SDCARD_SDIO_DMA
#define SDCARD_SDIO_DMA   1
#endif
#ifndef SDCARD_SDIO_DMA_CH
#define SDCARD_SDIO_DMA_CH 5u
#endif

#define SD_MAX_HZ         25000000u     /* default speed */

/* Commands. ACMDs have bit 7 set: sd_cmd() sends CMD55 first. */
#define CMD0    0u      /* GO_IDLE_STATE */
#define CMD2    2u      /* ALL_SEND_CID */
#define CMD3    3u      /* SEND_RELATIVE_ADDR */
#define CMD7    7u      /* SELECT_CARD */
#define CMD8    8u      /* SEND_IF_COND */
#define CMD9    9u      /* SEND_CSD */
#define CMD12   12u     /* STOP_TRANSMISSION */
#define CMD13   13u     /* SEND_STATUS */
#define CMD16   16u     /* SET_BLOCKLEN */
#define CMD17   17u     /* READ_SINGLE_BLOCK */
#define CMD18   18u     /* READ_MULTIPLE_BLOCK */
#define CMD24   24u     /* WRITE_BLOCK */
#define CMD25   25u     /* WRITE_MULTIPLE_BLOCK */
#define CMD55   55u     /* APP_CMD */
#define ACMD6   (0x80u | 6u)    /* SET_BUS_WIDTH */
#define ACMD13  (0x80u | 13u)   /* SD_STATUS */
#define ACMD23  (0x80u | 23u)   /* SET_WR_BLK_ERASE_COUNT */
#define ACMD41  (0x80u | 41u)   /* SD_SEND_OP_COND */

#define ACMD6_BUS_4BIT 2u        /* ACMD6 argument: 4-bit bus (0 = 1-bit) */

/* Response kinds for sd_cmd() */
#define RESP_NONE   0u
#define RESP_R1     1u          /* card status: index and error bits checked */
#define RESP_R6R7   2u          /* index checked, no status bits */
#define RESP_R3     3u          /* OCR: no index, no CRC */
#define RESP_R2     4u          /* 136 bits: CID / CSD */

/* Card status (R1) */
#define STATUS_OUT_OF_RANGE BIT(31)
#define STATUS_ERRORS       0xFDF98008uL    /* every error bit */
#define STATUS_STATE(st)    (((st) >> 9) & 0xFu)
#define STATE_TRAN          4u

/* OCR */
#define OCR_BUSY_DONE   BIT(31)         /* power-up finished */
#define OCR_CCS         BIT(30)         /* SDHC/SDXC */
#define OCR_VOLTAGES    0x00FF8000uL    /* 2.7..3.6 V */

/* Timeouts, ms (see sdcard_spi.c for why writes get 2 s) */
#define TMO_INIT_MS   1000u
#define TMO_WRITE_MS  2000u

#define STR_(x) #x
#define STR(x)  STR_(x)

static sdcard_info_t info;
static sdcard_diag_t diag;
static int ready;
static uint32_t rca;
static uint8_t last_cmd, last_index = 0xFFu;    /* for the diagnostics */
static uint32_t last_resp;
static uint32_t cur_lba;

/* Record an error for sdcard_get_diag() and return it. */
static int fail(int err)
{
    diag.err  = err;
    diag.cmd  = last_cmd;
    diag.r1   = last_index;
    diag.resp = last_resp;
    diag.lba  = cur_lba;
    diag.count++;
    return err;
}

/* An error from the flags of a data transfer. */
static int data_fail(uint32_t status)
{
    last_resp = status;
    if (status & (SDH_INT_DATA_CRC | SDH_INT_CRC_TOKEN)) {
        return fail(SDCARD_ERR_CRC);
    }
    return fail(SDCARD_ERR_TIMEOUT);
}

/* Send a command and check its response. ACMDs get CMD55 first. *resp
 * (may be NULL) gets the 32-bit response; R2 lands in info via read_r2(). */
static int sd_cmd(uint8_t cmd, uint32_t arg, uint32_t kind, uint32_t *resp)
{
    static const uint32_t io_for[] = {
        SDH_IO_CMD_NORESP, SDH_IO_CMD_SHORT, SDH_IO_CMD_SHORT, SDH_IO_CMD_SHORT, SDH_IO_CMD_LONG,
    };

    if (cmd & 0x80u) {
        int err = sd_cmd(CMD55, rca << 16, RESP_R1, NULL);
        if (err != SDCARD_OK) {
            last_cmd = cmd;
            return err;
        }
    }
    last_cmd = cmd;
    last_index = 0xFFu;
    last_resp = 0;

    uint32_t status = sdh_cmd(cmd & 0x3Fu, arg, io_for[kind]);
    if (status & (SDH_INT_NCR_TIMEOUT | SDH_STATUS_STUCK)) {
        last_resp = status;
        return fail(SDCARD_ERR_TIMEOUT);
    }
    if (kind == RESP_NONE) {
        return SDCARD_OK;
    }
    last_index = (uint8_t)sdh_resp_index();
    last_resp = sdh_resp();
    if (resp) {
        *resp = last_resp;
    }
    if ((kind == RESP_R3) || (kind == RESP_R2)) {
        return SDCARD_OK;               /* these carry 0x3F and no valid CRC */
    }
    if (status & SDH_INT_CMD_CRC) {
        return fail(SDCARD_ERR_CRC);
    }
    if (last_index != (cmd & 0x3Fu)) {
        return fail(SDCARD_ERR_CMD);
    }
    if ((kind == RESP_R1) && (last_resp & STATUS_ERRORS)) {
        return fail(SDCARD_ERR_CMD);
    }
    return SDCARD_OK;
}

/* Poll CMD13 until the card is back in the transfer state (done
 * programming). ignore = status bits not to treat as errors. */
static int wait_tran(uint32_t ms, uint32_t ignore)
{
    uint32_t last = CPU_TICKS;
    uint32_t elapsed = 0;
    uint32_t limit = cpu_ms_to_cycles(ms);
    uint32_t status;

    for (;;) {
        int err = sd_cmd(CMD13, rca << 16, RESP_R6R7, &status);
        if (err != SDCARD_OK) {
            return err;
        }
        if (status & STATUS_ERRORS & ~ignore) {
            return fail(SDCARD_ERR_WRITE);
        }
        if (STATUS_STATE(status) == STATE_TRAN) {
            return SDCARD_OK;
        }
        elapsed += cpu_cycles_step(&last);
        if (elapsed >= limit) {
            return fail(SDCARD_ERR_TIMEOUT);
        }
    }
}

/* ---- info ----------------------------------------------------------------------- */

int sdcard_present(void)
{
    if (SDCARD_SDIO_CD == GPIO_NO_PIN) {
        return 1;
    }
    return gpio_read(SDCARD_SDIO_CD) == GPIO_LOW;
}

int sdcard_ready(void)
{
    return ready;
}

const sdcard_info_t *sdcard_get_info(void)
{
    return &info;
}

const sdcard_diag_t *sdcard_get_diag(void)
{
    return &diag;
}

const char *sdcard_bus_info(void)
{
    return "SDIO " STR(SDCARD_SDIO_WIDTH) "-bit, CLK " STR(SDCARD_SDIO_CLK) ", CMD "
           STR(SDCARD_SDIO_CMD) ", DAT0 " STR(SDCARD_SDIO_D0)
#if SDCARD_SDIO_WIDTH == SDH_WIDTH_4BIT
           ", DAT1-3 " STR(SDCARD_SDIO_D1) " " STR(SDCARD_SDIO_D2) " " STR(SDCARD_SDIO_D3)
#endif
           "; CRC by the controller"
#if SDCARD_SDIO_DMA
           "; multi-block by DMA"
#else
           "; one block at a time, CPU copy"
#endif
           ;
}

uint32_t sdcard_set_clock(uint32_t hz)
{
    info.clock_hz = sdh_set_clock((hz > SD_MAX_HZ) ? SD_MAX_HZ : hz);
    return info.clock_hz;
}

/* ---- init ----------------------------------------------------------------------- */

/* Allocation unit from the SD status (ACMD13), in sectors; 0 if unknown. */
static uint32_t read_au(void)
{
    uint8_t st[64];
    uint32_t au = 0;

    if (sd_cmd(ACMD13, 0, RESP_R1, NULL) == SDCARD_OK) {
        uint32_t status = sdh_read_block(st, sizeof st);
        if (!(status & SDH_ERR_MASK)) {
            uint32_t code = st[10] >> 4;    /* AU_SIZE: 16 KB << (code - 1) */
            if ((code >= 1u) && (code <= 9u)) {
                au = 32u << (code - 1u);
            } else if (code >= 10u) {
                static const uint16_t big[] = { 12, 16, 24, 32, 64 };   /* MB */
                au = (uint32_t)big[code - 10u] * 2048u;
            }
        }
    }
    return au;
}

/* Identification (CMD0 .. CMD3) at the slow clock. */
static int identify(void)
{
    uint32_t resp = 0;
    int v2 = 0;
    int err;

    for (int byte = 0; byte < 10; byte++) {
        sdh_clocks();                   /* 74+ clocks after power-up */
    }
    sd_cmd(CMD0, 0, RESP_NONE, NULL);
    delay_ms(1);

    /* SD v2+ answer CMD8 with the check pattern. v1 cards do not answer:
     * the controller is reset after the timeout to be safe. */
    err = sd_cmd(CMD8, 0x1AAu, RESP_R6R7, &resp);
    if ((err == SDCARD_OK) && ((resp & 0xFFFu) == 0x1AAu)) {
        v2 = 1;
    } else if (err == SDCARD_OK) {
        return fail(SDCARD_ERR_UNUSABLE);    /* voltage not accepted */
    } else {
        sdh_init();
        sd_cmd(CMD0, 0, RESP_NONE, NULL);
    }

    uint32_t last = CPU_TICKS;
    uint32_t elapsed = 0;
    uint32_t limit = cpu_ms_to_cycles(TMO_INIT_MS);
    do {
        err = sd_cmd(ACMD41, (v2 ? OCR_CCS : 0u) | OCR_VOLTAGES, RESP_R3, &resp);
        if (err != SDCARD_OK) {
            return err;                 /* no answer: no card, or an MMC card */
        }
        elapsed += cpu_cycles_step(&last);
        if (elapsed >= limit) {
            return fail(SDCARD_ERR_TIMEOUT);
        }
    } while (!(resp & OCR_BUSY_DONE));
    info.ocr = resp;
    info.type = !v2 ? SDCARD_TYPE_SDV1 : ((resp & OCR_CCS) ? SDCARD_TYPE_SDHC : SDCARD_TYPE_SDSC);

    err = sd_cmd(CMD2, 0, RESP_R2, NULL);
    if (err == SDCARD_OK) {
        sdh_resp_long(info.cid);
        err = sd_cmd(CMD3, 0, RESP_R6R7, &resp);
    }
    if (err == SDCARD_OK) {
        rca = resp >> 16;
        err = sd_cmd(CMD9, rca << 16, RESP_R2, NULL);
    }
    if (err == SDCARD_OK) {
        sdh_resp_long(info.csd);
    }
    /* The controller keeps 15 of the 16 bytes; put the CRC7 back. */
    info.cid[15] = (uint8_t)((sdcard_crc7(info.cid, 15) << 1) | 1u);
    info.csd[15] = (uint8_t)((sdcard_crc7(info.csd, 15) << 1) | 1u);
    return err;
}

/* Select the card, set the bus width and block length. */
static int configure(void)
{
    int err = sd_cmd(CMD7, rca << 16, RESP_R1, NULL);

    if (err == SDCARD_OK) {
        err = wait_tran(TMO_WRITE_MS, 0);
    }
#if SDCARD_SDIO_WIDTH == SDH_WIDTH_4BIT
    if (err == SDCARD_OK) {
        err = sd_cmd(ACMD6, ACMD6_BUS_4BIT, RESP_R1, NULL);
        if (err == SDCARD_OK) {
            sdh_set_width(SDH_WIDTH_4BIT);
        }
    }
#endif
    if ((err == SDCARD_OK) && (info.type != SDCARD_TYPE_SDHC)) {
        err = sd_cmd(CMD16, SDCARD_SECTOR_SIZE, RESP_R1, NULL);
    }
    return err;
}

int sdcard_init(void)
{
    int err;

    ready = 0;
    rca = 0;
    cur_lba = 0;
    memset(&info, 0, sizeof info);
    memset(&diag, 0, sizeof diag);

    if (SDCARD_SDIO_CD != GPIO_NO_PIN) {
        gpio_mode(SDCARD_SDIO_CD, GPIO_INPUT_PULLUP);
    }
    if (!sdcard_present()) {
        return fail(SDCARD_ERR_NOCARD);
    }

#if SDCARD_SDIO_WIDTH == SDH_WIDTH_4BIT
    sdh_pins(SDCARD_SDIO_CLK, SDCARD_SDIO_CMD, SDCARD_SDIO_D0, SDCARD_SDIO_D1, SDCARD_SDIO_D2,
             SDCARD_SDIO_D3, SDCARD_SDIO_AF);
#else
    sdh_pins(SDCARD_SDIO_CLK, SDCARD_SDIO_CMD, SDCARD_SDIO_D0, GPIO_NO_PIN, GPIO_NO_PIN,
             GPIO_NO_PIN, SDCARD_SDIO_AF);
#endif
    sdh_init();
    info.clock_hz = sdh_set_clock(SDCARD_SDIO_INIT_HZ);
#if SDCARD_SDIO_DMA
    if (!(CLK->GATE_EN & CLK_GATE_DMA)) {
        dma_init();                     /* leave it alone if already in use */
    }
#endif
    delay_ms(2);                        /* power-up time */

    err = identify();
    if (err == SDCARD_ERR_TIMEOUT) {
        err = SDCARD_ERR_NOCARD;        /* nothing answered the identification */
        diag.err = err;
    }
    if (err == SDCARD_OK) {
        sdcard_set_clock(SDCARD_SDIO_HZ);
        err = configure();
    }
    if (err != SDCARD_OK) {
        info.type = SDCARD_TYPE_NONE;
        return err;
    }

    info.sectors = sdcard_csd_sectors(info.csd, info.type);
    info.erase_sectors = sdcard_csd_erase_sectors(info.csd, info.type);
    uint32_t au = read_au();
    if (au) {
        info.erase_sectors = au;
    }
    memset(&diag, 0, sizeof diag);      /* init went fine: start clean */
    ready = 1;
    return SDCARD_OK;
}

/* ---- block access --------------------------------------------------------------- */

static uint32_t card_addr(uint32_t lba)
{
    return (info.type == SDCARD_TYPE_SDHC) ? lba : (lba * SDCARD_SECTOR_SIZE);
}

static int check_range(uint32_t lba, uint32_t count)
{
    cur_lba = lba;
    if (!ready || (count == 0u) || (lba >= info.sectors) || (count > (info.sectors - lba))) {
        last_resp = count;
        return fail(SDCARD_ERR_PARAM);
    }
    return SDCARD_OK;
}

/* Sectors to move in one go: up to a DMA run, or 1 without DMA. */
static uint32_t run_length(const void *buf, uint32_t count)
{
    if (!SDCARD_SDIO_DMA || (count == 1u) || !sdh_dma_ok(buf)) {
        return 1u;
    }
    return (count > SDH_DMA_MAX_BLOCKS) ? SDH_DMA_MAX_BLOCKS : count;
}

/* End a multi-block transfer: CMD12, then wait until the card is back in
 * the transfer state. Reading or writing up to the last sector may set
 * OUT_OF_RANGE, which the spec says to ignore. */
static int stop_transfer(uint32_t lba, uint32_t blocks)
{
    uint32_t ignore = ((lba + blocks) == info.sectors) ? STATUS_OUT_OF_RANGE : 0u;
    int err = sd_cmd(CMD12, 0, RESP_R6R7, NULL);
    int wait_err = wait_tran(TMO_WRITE_MS, ignore);

    return (err != SDCARD_OK) ? err : wait_err;
}

/* One attempt at a run of sectors; *done = sectors read. */
static int read_run(uint32_t lba, uint8_t *buf, uint32_t blocks, uint32_t *done)
{
    uint32_t status;
    int err;

    *done = 0;
    cur_lba = lba;
    if (blocks == 1u) {
        err = sd_cmd(CMD17, card_addr(lba), RESP_R1, NULL);
        if (err == SDCARD_OK) {
            status = sdh_read_block(buf, SDCARD_SECTOR_SIZE);
            err = (status & SDH_ERR_MASK) ? data_fail(status) : SDCARD_OK;
        }
    } else {
        err = sd_cmd(CMD18, card_addr(lba), RESP_R1, NULL);
        if (err == SDCARD_OK) {
            status = sdh_read_blocks_dma(SDCARD_SDIO_DMA_CH, buf, blocks);
            err = (status & SDH_ERR_MASK) ? data_fail(status) : SDCARD_OK;
            int stop_err = stop_transfer(lba, blocks);
            if (err == SDCARD_OK) {
                err = stop_err;
            }
        }
    }
    if (err == SDCARD_OK) {
        *done = blocks;
    }
    return err;
}

int sdcard_read(uint32_t lba, void *buf, uint32_t count)
{
    uint8_t *pos = (uint8_t *)buf;
    int err = check_range(lba, count);

    for (int tries = 0; (err == SDCARD_OK) && (count > 0u); ) {
        uint32_t done;
        err = read_run(lba, pos, run_length(pos, count), &done);
        lba += done;
        pos += done * SDCARD_SECTOR_SIZE;
        count -= done;
        if (done) {
            tries = 0;                  /* progress: start counting afresh */
        }
        if (((err == SDCARD_ERR_CRC) || (err == SDCARD_ERR_TIMEOUT)) &&
            (++tries < SDCARD_SDIO_RETRIES)) {
            err = SDCARD_OK;
        }
    }
    return err;
}

/* One attempt at writing a run of sectors. */
static int write_run(uint32_t lba, const uint8_t *buf, uint32_t blocks)
{
    uint32_t status;
    int err;

    cur_lba = lba;
    if (blocks == 1u) {
        err = sd_cmd(CMD24, card_addr(lba), RESP_R1, NULL);
        if (err == SDCARD_OK) {
            status = sdh_write_block(buf, SDCARD_SECTOR_SIZE);
            err = (status & SDH_ERR_MASK) ? data_fail(status) : SDCARD_OK;
            int wait_err = wait_tran(TMO_WRITE_MS, 0);   /* programming */
            if (err == SDCARD_OK) {
                err = wait_err;
            }
        }
    } else {
        sd_cmd(ACMD23, blocks, RESP_R1, NULL);           /* pre-erase hint */
        err = sd_cmd(CMD25, card_addr(lba), RESP_R1, NULL);
        if (err == SDCARD_OK) {
            status = sdh_write_blocks_dma(SDCARD_SDIO_DMA_CH, buf, blocks);
            err = (status & SDH_ERR_MASK) ? data_fail(status) : SDCARD_OK;
            int stop_err = stop_transfer(lba, blocks);
            if (err == SDCARD_OK) {
                err = stop_err;
            }
        }
    }
    return err;
}

int sdcard_write(uint32_t lba, const void *buf, uint32_t count)
{
    const uint8_t *pos = (const uint8_t *)buf;
    int err = check_range(lba, count);

    /* A failed run is written again whole: writing the same data twice is
     * harmless. */
    for (int tries = 0; (err == SDCARD_OK) && (count > 0u); ) {
        uint32_t blocks = run_length(pos, count);
        err = write_run(lba, pos, blocks);
        if (err == SDCARD_OK) {
            lba += blocks;
            pos += blocks * SDCARD_SECTOR_SIZE;
            count -= blocks;
            tries = 0;
        } else if (((err == SDCARD_ERR_CRC) || (err == SDCARD_ERR_TIMEOUT)) &&
                   (++tries < SDCARD_SDIO_RETRIES)) {
            err = SDCARD_OK;
        }
    }
    return err;
}

int sdcard_sync(void)
{
    if (!ready) {
        return SDCARD_ERR_PARAM;
    }
    return wait_tran(TMO_WRITE_MS, 0);
}
