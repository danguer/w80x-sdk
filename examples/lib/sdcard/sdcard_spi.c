/*
 * sdcard_spi.c - SD / MMC card in SPI mode on the LSPI. Implements sdcard.h.
 *
 * Wiring (defaults: the free SPI pins on the HLK-W801-KIT):
 *
 *   card        W80x
 *   SCK         PB2   SDCARD_SPI_SCK   (LSPI SCK, AF2)
 *   MISO        PB3   SDCARD_SPI_MISO  (LSPI MISO, AF2, pull-up enabled)
 *   MOSI        PA7   SDCARD_SPI_MOSI  (LSPI MOSI, AF2)
 *   CS          PB4   SDCARD_SPI_CS    (plain GPIO)
 *   CD          none  SDCARD_SPI_CD    (optional card detect, low = card in)
 *   VDD 3.3 V, GND
 *
 * Override any of them on the command line, e.g.
 *   make CFLAGS='-DSDCARD_SPI_CS=PB6 -DSDCARD_SPI_CD=PB7'
 * The SCK/MISO/MOSI pins must be an LSPI set (see w80x_spi.h) and
 * SDCARD_SPI_AF their alternate function.
 *
 * Clock: 400 kHz while the card initializes, then SDCARD_SPI_HZ (default
 * 20 MHz, the LSPI maximum with a 40 MHz APB; SPI mode allows 25 MHz).
 * With long jumper wires, try a lower rate, e.g. -DSDCARD_SPI_HZ=8000000.
 *
 * CRC: on by default (SDCARD_SPI_CRC=1). The card checks commands and
 * written blocks (CMD59), and every block read is checked here, so a bad
 * wire shows up as SDCARD_ERR_CRC instead of corrupt files. Reads and
 * writes are retried (SDCARD_SPI_RETRIES) after a CRC error or a timeout. Build with
 * -DSDCARD_SPI_CRC=0 to skip the checks for a little more speed, or with
 * -DSDCARD_HW_CRC=1 to compute the data CRCs on the crypto engine (see
 * sdcard.c) instead of in software.
 *
 * DMA: build with -DSDCARD_SPI_DMA=1 to move the 512-byte data blocks with
 * DMA (spi_transfer_dma() in w80x_spi.h) instead of copying them through
 * the SPI FIFO 32 bytes at a time. It uses DMA channels SDCARD_SPI_DMA_TX_CH
 * and SDCARD_SPI_DMA_RX_CH (default 6 and 7). Buffers must be word-aligned
 * SRAM to use DMA; others fall back to the FIFO copy. Commands and tokens
 * always use the FIFO.
 *
 * The sequence follows the SD Physical Layer Simplified Spec. ch. 7 (SPI
 * mode) and ChaN's FatFs mmc_avr sample: CMD0, CMD8, ACMD41 (CMD1 for MMC),
 * CMD58, CMD16 for byte-addressed cards, CMD9/CMD10.
 *
 * Polled, one card. Not reentrant: call from one context only.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "w80x.h"
#include "w80x_gpio.h"
#include "w80x_spi.h"
#include "w80x_utils.h"
#include "w80x_dma.h"
#include "sdcard.h"

#ifndef SDCARD_SPI_SCK
#define SDCARD_SPI_SCK   PB2
#endif
#ifndef SDCARD_SPI_MISO
#define SDCARD_SPI_MISO  PB3
#endif
#ifndef SDCARD_SPI_MOSI
#define SDCARD_SPI_MOSI  PA7
#endif
#ifndef SDCARD_SPI_AF
#define SDCARD_SPI_AF    GPIO_AF2
#endif
#ifndef SDCARD_SPI_CS
#define SDCARD_SPI_CS    PB4
#endif
#ifndef SDCARD_SPI_CD
#define SDCARD_SPI_CD    0xFFu       /* 0xFF = no card-detect pin */
#endif
#ifndef SDCARD_SPI_INIT_HZ
#define SDCARD_SPI_INIT_HZ  400000u
#endif
#ifndef SDCARD_SPI_HZ
#define SDCARD_SPI_HZ    20000000u
#endif
#ifndef SDCARD_SPI_CRC
#define SDCARD_SPI_CRC   1
#endif
#ifndef SDCARD_SPI_RETRIES
#define SDCARD_SPI_RETRIES  3
#endif
#ifndef SDCARD_SPI_DMA
#define SDCARD_SPI_DMA   0
#endif
#ifndef SDCARD_SPI_DMA_TX_CH
#define SDCARD_SPI_DMA_TX_CH  6u
#endif
#ifndef SDCARD_SPI_DMA_RX_CH
#define SDCARD_SPI_DMA_RX_CH  7u
#endif

/* Data blocks: DMA or the FIFO copy. */
#if SDCARD_SPI_DMA
#define data_read(buf, len)  spi_transfer_dma(SDCARD_SPI_DMA_TX_CH, SDCARD_SPI_DMA_RX_CH, NULL, (buf), (len))
#define data_write(buf, len) spi_transfer_dma(SDCARD_SPI_DMA_TX_CH, SDCARD_SPI_DMA_RX_CH, (buf), NULL, (len))
#else
#define data_read(buf, len)  spi_read((buf), (len))
#define data_write(buf, len) spi_write((buf), (len))
#endif

/* Commands. ACMDs have bit 7 set: sd_cmd() sends CMD55 first. */
#define CMD0    0u      /* GO_IDLE_STATE */
#define CMD1    1u      /* SEND_OP_COND (MMC) */
#define CMD8    8u      /* SEND_IF_COND */
#define CMD9    9u      /* SEND_CSD */
#define CMD10   10u     /* SEND_CID */
#define CMD12   12u     /* STOP_TRANSMISSION */
#define CMD13   13u     /* SEND_STATUS */
#define CMD16   16u     /* SET_BLOCKLEN */
#define CMD17   17u     /* READ_SINGLE_BLOCK */
#define CMD18   18u     /* READ_MULTIPLE_BLOCK */
#define CMD24   24u     /* WRITE_BLOCK */
#define CMD25   25u     /* WRITE_MULTIPLE_BLOCK */
#define CMD55   55u     /* APP_CMD */
#define CMD58   58u     /* READ_OCR */
#define CMD59   59u     /* CRC_ON_OFF */
#define ACMD13  (0x80u | 13u)   /* SD_STATUS */
#define ACMD23  (0x80u | 23u)   /* SET_WR_BLK_ERASE_COUNT */
#define ACMD41  (0x80u | 41u)   /* SD_SEND_OP_COND */

/* R1 bits */
#define R1_IDLE         0x01u
#define R1_ILLEGAL_CMD  0x04u
#define R1_CRC_ERR      0x08u

/* Data tokens */
#define TOKEN_START       0xFEu   /* single read/write, multiple read */
#define TOKEN_START_MULTI 0xFCu   /* multiple write */
#define TOKEN_STOP        0xFDu   /* end of multiple write */

/* R2 (CMD13) second byte */
#define R2_OUT_OF_RANGE 0x80u

/* Timeouts, ms. The spec allows 100 ms for a read and 250 ms (SDXC: 500 ms)
 * for a write, but cheap cards take longer: one 32 GB card was seen busy
 * for over 1 s on its first write to a fresh area. ACMD41 can take up to
 * 1 s. */
#define TMO_INIT_MS   1000u
#define TMO_READ_MS   200u
#define TMO_WRITE_MS  2000u
#define TMO_STOP_MS   3000u     /* last wait before the stop token, see stop_write() */

static sdcard_info_t info;
static sdcard_diag_t diag;
static int ready;
static uint8_t last_cmd, last_r1 = 0xFFu;   /* for the diagnostics */
static uint32_t cur_lba;

/* Record an error for sdcard_get_diag() and return it. */
static int fail(int err, uint32_t resp)
{
    diag.err  = err;
    diag.cmd  = last_cmd;
    diag.r1   = last_r1;
    diag.resp = resp;
    diag.lba  = cur_lba;
    diag.count++;
    return err;
}

/* ---- timeouts on CORET ---------------------------------------------------------
 * CORET is a 24-bit down counter (70 ms at 240 MHz), so elapsed cycles are
 * added up on every poll; the polls are microseconds apart. */
typedef struct {
    uint32_t last;
    uint32_t elapsed;
    uint32_t limit;
} tmo_t;

static void tmo_start(tmo_t *tmo, uint32_t ms)
{
    tmo->last = CPU_TICKS;
    tmo->elapsed = 0;
    tmo->limit = cpu_ms_to_cycles(ms);
}

static int tmo_expired(tmo_t *tmo)
{
    tmo->elapsed += cpu_cycles_step(&tmo->last);
    return tmo->elapsed >= tmo->limit;
}

/* ---- bus ------------------------------------------------------------------------ */

static uint8_t rx_byte(void)
{
    return spi_xfer(0xFFu);
}

/* Wait until the card stops holding MISO low (busy). 1 = ready. */
static int wait_ready(uint32_t ms)
{
    tmo_t tmo;

    tmo_start(&tmo, ms);
    while (rx_byte() != 0xFFu) {
        if (tmo_expired(&tmo)) {
            return 0;
        }
    }
    return 1;
}

static void deselect(void)
{
    gpio_set(SDCARD_SPI_CS);
    rx_byte();                  /* one more clock so the card releases MISO */
}

/* CS low and wait for the card to be ready. 1 = selected. */
static int select_card(void)
{
    gpio_clear(SDCARD_SPI_CS);
    rx_byte();
    if (wait_ready(TMO_WRITE_MS)) {
        return 1;
    }
    deselect();
    return 0;
}

/* Send a command and return R1 (0xFF: no response). The card stays selected
 * so the caller can read the rest of the response or data; call
 * deselect() when done. */
static uint8_t sd_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t r1 = 0xFFu;
    uint8_t name = cmd;

    if (cmd & 0x80u) {
        cmd &= 0x7Fu;
        r1 = sd_cmd(CMD55, 0);
        last_cmd = name;
        if (r1 > R1_IDLE) {
            return r1;
        }
    }
    last_cmd = name;
    last_r1 = 0xFFu;
    if (cmd != CMD12) {
        deselect();
        if (!select_card()) {
            return 0xFFu;
        }
    }

    uint8_t frame[6];
    frame[0] = (uint8_t)(0x40u | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)arg;
    frame[5] = (uint8_t)((sdcard_crc7(frame, 5) << 1) | 1u);
    spi_write(frame, sizeof frame);

    if (cmd == CMD12) {
        rx_byte();              /* stuff byte */
    }
    /* R1 comes within 8 bytes (NCR); bit 7 is 0 in a response */
    for (int tries = 0; tries < 10; tries++) {
        r1 = rx_byte();
        if (!(r1 & 0x80u)) {
            break;
        }
    }
    last_r1 = r1;
    return r1;
}

/* Receive a data block of len bytes after a read command. */
static int rx_block(uint8_t *buf, uint32_t len)
{
    tmo_t tmo;
    uint8_t tok;

    tmo_start(&tmo, TMO_READ_MS);
    while ((tok = rx_byte()) == 0xFFu) {
        if (tmo_expired(&tmo)) {
            return fail(SDCARD_ERR_TIMEOUT, tok);
        }
    }
    if (tok != TOKEN_START) {
        return fail(SDCARD_ERR_CMD, tok);       /* data error token */
    }
    if (data_read(buf, len)) {
        return fail(SDCARD_ERR_TIMEOUT, tok);
    }
    uint8_t crc_bytes[2];
    spi_read(crc_bytes, 2);
#if SDCARD_SPI_CRC
    uint16_t got = (uint16_t)(((uint16_t)crc_bytes[0] << 8) | crc_bytes[1]);
    if (got != sdcard_crc16(0, buf, len)) {
        return fail(SDCARD_ERR_CRC, got);
    }
#endif
    return SDCARD_OK;
}

/* Send one 512-byte block with the given start token. */
static int tx_block(const uint8_t *buf, uint8_t token)
{
    if (!wait_ready(TMO_WRITE_MS)) {
        return fail(SDCARD_ERR_TIMEOUT, 0x00u);
    }
    spi_xfer(token);

    uint16_t crc = SDCARD_SPI_CRC ? sdcard_crc16(0, buf, SDCARD_SECTOR_SIZE) : 0xFFFFu;
    uint8_t crc_bytes[2] = { (uint8_t)(crc >> 8), (uint8_t)crc };
    if (data_write(buf, SDCARD_SECTOR_SIZE)) {
        return fail(SDCARD_ERR_TIMEOUT, 0xFFu);
    }
    spi_write(crc_bytes, 2);

    uint8_t resp = rx_byte();           /* data response: xxx0sss1 */
    if ((resp & 0x1Fu) == 0x05u) {
        return SDCARD_OK;               /* accepted */
    }
    return fail(((resp & 0x1Fu) == 0x0Bu) ? SDCARD_ERR_CRC : SDCARD_ERR_WRITE, resp);
}

/* End a multiple-block write. The stop token has to reach the card even
 * after an error: without it the card keeps waiting for data and ignores
 * every command, CMD0 included, until its power is removed. A busy card
 * ignores the token, so wait for it as long as is reasonable first. */
static int stop_write(void)
{
    int ok = wait_ready(TMO_STOP_MS);

    spi_xfer(TOKEN_STOP);
    rx_byte();                          /* busy starts one byte after the token */
    if (!wait_ready(TMO_WRITE_MS) || !ok) {
        return fail(SDCARD_ERR_TIMEOUT, 0x00u);
    }
    return SDCARD_OK;
}

/* ---- init ----------------------------------------------------------------------- */

/* Read a register (CSD, CID) sent as a 16-byte data block. */
static int read_reg(uint8_t cmd, uint8_t out[16])
{
    int err = (sd_cmd(cmd, 0) == 0) ? rx_block(out, 16) : fail(SDCARD_ERR_CMD, 0xFFu);
    deselect();
    return err;
}

static uint32_t read_ocr(void)
{
    uint8_t ocr[4] = { 0 };

    spi_read(ocr, 4);
    return ((uint32_t)ocr[0] << 24) | ((uint32_t)ocr[1] << 16) | ((uint32_t)ocr[2] << 8) | ocr[3];
}

/* Allocation unit from the SD status (ACMD13), in sectors; 0 if unknown. */
static uint32_t read_au(void)
{
    uint8_t st[64];
    uint32_t au = 0;

    if (sd_cmd(ACMD13, 0) == 0) {
        rx_byte();                       /* second byte of R2 */
        if (rx_block(st, sizeof st) == SDCARD_OK) {
            uint32_t code = st[10] >> 4;  /* AU_SIZE: 16 KB << (code - 1) */
            if ((code >= 1u) && (code <= 9u)) {
                au = 32u << (code - 1u);
            } else if (code >= 10u) {
                static const uint16_t big[] = { 12, 16, 24, 32, 64 };   /* MB */
                au = (uint32_t)big[code - 10u] * 2048u;
            }
        }
    }
    deselect();
    return au;
}

int sdcard_present(void)
{
    if (SDCARD_SPI_CD == 0xFFu) {
        return 1;
    }
    return gpio_read(SDCARD_SPI_CD) == GPIO_LOW;
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

#define STR_(x) #x
#define STR(x)  STR_(x)

const char *sdcard_bus_info(void)
{
    return "SPI, SCK " STR(SDCARD_SPI_SCK) ", MISO " STR(SDCARD_SPI_MISO) ", MOSI "
           STR(SDCARD_SPI_MOSI) ", CS " STR(SDCARD_SPI_CS)
#if !SDCARD_SPI_CRC
           "; no data CRC"
#elif defined(SDCARD_HW_CRC) && SDCARD_HW_CRC
           "; data CRC on the crypto engine"
#else
           "; data CRC in software"
#endif
#if SDCARD_SPI_DMA
           "; data blocks by DMA"
#else
           "; data blocks through the SPI FIFO"
#endif
           ;
}

uint32_t sdcard_set_clock(uint32_t hz)
{
    info.clock_hz = spi_set_clock(hz);
    return info.clock_hz;
}

/* The card keeps its power when the W80x resets, so it can still be in a
 * transfer started before the reset: a multiple-block write (ignores
 * commands until it gets the stop token) or read (until CMD12). Both are
 * harmless to a card in SPI mode that is idle. */
static void unstick(void)
{
    gpio_clear(SDCARD_SPI_CS);
    wait_ready(TMO_STOP_MS);            /* let pending programming finish */
    spi_xfer(TOKEN_STOP);
    rx_byte();
    wait_ready(TMO_WRITE_MS);
    sd_cmd(CMD12, 0);
    wait_ready(TMO_WRITE_MS);
    deselect();
}

static int init_card(void)
{
    sdcard_type_t type = SDCARD_TYPE_NONE;
    uint8_t r1 = 0xFFu;
    tmo_t tmo;

    /* 74+ clocks with CS high (and MOSI high) put the card in native mode,
     * ready for CMD0 */
    gpio_set(SDCARD_SPI_CS);
    for (int byte = 0; byte < 10; byte++) {
        rx_byte();
    }

    /* CMD0 with CS low switches to SPI mode. If the card does not answer,
     * it may be stuck in a transfer from before a reset: end that, retry. */
    for (int tries = 0; (tries < 3) && (r1 != R1_IDLE); tries++) {
        r1 = sd_cmd(CMD0, 0);
    }
    for (int tries = 0; (tries < 10) && (r1 != R1_IDLE); tries++) {
        if ((tries % 5) == 0) {
            unstick();
        }
        r1 = sd_cmd(CMD0, 0);
    }
    if (r1 != R1_IDLE) {
        deselect();
        return fail(SDCARD_ERR_NOCARD, r1);
    }

    tmo_start(&tmo, TMO_INIT_MS);
    if (sd_cmd(CMD8, 0x1AAu) == R1_IDLE) {
        /* SD v2+: R7 echoes the check pattern and voltage range */
        uint32_t r7 = read_ocr();
        if ((r7 & 0xFFFu) != 0x1AAu) {
            deselect();
            return fail(SDCARD_ERR_UNUSABLE, r7);
        }
        while ((r1 = sd_cmd(ACMD41, 1uL << 30)) != 0) {     /* HCS: we take SDHC */
            if ((r1 > R1_IDLE) || tmo_expired(&tmo)) {
                deselect();
                return fail(SDCARD_ERR_TIMEOUT, r1);
            }
        }
        if (sd_cmd(CMD58, 0) != 0) {
            deselect();
            return fail(SDCARD_ERR_CMD, 0xFFu);
        }
        info.ocr = read_ocr();
        type = (info.ocr & (1uL << 30)) ? SDCARD_TYPE_SDHC : SDCARD_TYPE_SDSC;   /* CCS */
    } else {
        /* SD v1 answers ACMD41; MMC rejects it and needs CMD1 */
        uint8_t op = CMD1;
        type = SDCARD_TYPE_MMC;
        if (sd_cmd(ACMD41, 0) <= R1_IDLE) {
            op = ACMD41;
            type = SDCARD_TYPE_SDV1;
        }
        while ((r1 = sd_cmd(op, 0)) != 0) {
            if ((r1 > R1_IDLE) || tmo_expired(&tmo)) {
                deselect();
                return fail((r1 > R1_IDLE) ? SDCARD_ERR_UNUSABLE : SDCARD_ERR_TIMEOUT, r1);
            }
        }
        if (sd_cmd(CMD58, 0) == 0) {
            info.ocr = read_ocr();
        }
    }

    if ((type != SDCARD_TYPE_SDHC) && (sd_cmd(CMD16, SDCARD_SECTOR_SIZE) != 0)) {
        deselect();
        return fail(SDCARD_ERR_CMD, 0xFFu);
    }
    /* CRC checking on the card side; on our side it is a build option */
    if (sd_cmd(CMD59, SDCARD_SPI_CRC ? 1u : 0u) > R1_IDLE) {
        deselect();
        return fail(SDCARD_ERR_CMD, 0xFFu);
    }
    deselect();
    info.type = type;
    return SDCARD_OK;
}

int sdcard_init(void)
{
    int err;

    ready = 0;
    cur_lba = 0;
    memset(&info, 0, sizeof info);
    memset(&diag, 0, sizeof diag);

    if (SDCARD_SPI_CD != 0xFFu) {
        gpio_mode(SDCARD_SPI_CD, GPIO_INPUT_PULLUP);
    }
    if (!sdcard_present()) {
        return fail(SDCARD_ERR_NOCARD, 0xFFu);
    }

    gpio_output(SDCARD_SPI_CS, GPIO_HIGH);
    gpio_mode(SDCARD_SPI_MISO, GPIO_INPUT_PULLUP);  /* MISO floats until the card drives it */
    spi_pins(SDCARD_SPI_SCK, SDCARD_SPI_MISO, SDCARD_SPI_MOSI, SDCARD_SPI_AF);
    spi_init(SDCARD_SPI_INIT_HZ, SPI_MODE0);
#if SDCARD_SPI_DMA
    if (!(CLK->GATE_EN & CLK_GATE_DMA)) {
        dma_init();                                 /* leave it alone if already in use */
    }
#endif
    delay_ms(2);                                    /* power-up time */

    err = init_card();
    if (err == SDCARD_OK) {
        err = read_reg(CMD9, info.csd);
    }
    if (err == SDCARD_OK) {
        err = read_reg(CMD10, info.cid);
    }
    if (err != SDCARD_OK) {
        info.type = SDCARD_TYPE_NONE;
        return err;
    }

    info.sectors = sdcard_csd_sectors(info.csd, info.type);
    info.erase_sectors = sdcard_csd_erase_sectors(info.csd, info.type);
    if ((info.type == SDCARD_TYPE_SDHC) || (info.type == SDCARD_TYPE_SDSC)) {
        uint32_t au = read_au();
        if (au) {
            info.erase_sectors = au;
        }
    }
    sdcard_set_clock(SDCARD_SPI_HZ);
    memset(&diag, 0, sizeof diag);                  /* init went fine: start clean */
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
        return fail(SDCARD_ERR_PARAM, count);
    }
    return SDCARD_OK;
}

/* One attempt; *done = sectors read successfully. */
static int read_blocks(uint32_t lba, uint8_t *buf, uint32_t count, uint32_t *done)
{
    int err = SDCARD_OK;

    *done = 0;
    cur_lba = lba;
    if (count == 1u) {
        err = (sd_cmd(CMD17, card_addr(lba)) == 0) ? rx_block(buf, SDCARD_SECTOR_SIZE)
                                                 : fail(SDCARD_ERR_CMD, 0xFFu);
        if (err == SDCARD_OK) {
            *done = 1;
        }
    } else if (sd_cmd(CMD18, card_addr(lba)) == 0) {
        while (*done < count) {
            cur_lba = lba + *done;
            err = rx_block(buf, SDCARD_SECTOR_SIZE);
            if (err != SDCARD_OK) {
                break;
            }
            buf += SDCARD_SECTOR_SIZE;
            (*done)++;
        }
        sd_cmd(CMD12, 0);
        /* the card may stay busy briefly after the stop */
        if (!wait_ready(TMO_WRITE_MS) && (err == SDCARD_OK)) {
            err = fail(SDCARD_ERR_TIMEOUT, 0x00u);
        }
        /* Reading up to the last sector may set OUT_OF_RANGE (the spec says
         * to ignore it). The flag stays until the status is read, and the
         * CMD13 after the next write would then report it: read and drop
         * the status now. */
        if ((lba + count) == info.sectors) {
            sd_cmd(CMD13, 0);
            rx_byte();
        }
    } else {
        err = fail(SDCARD_ERR_CMD, 0xFFu);
    }
    deselect();
    return err;
}

int sdcard_read(uint32_t lba, void *buf, uint32_t count)
{
    uint8_t *pos = (uint8_t *)buf;
    int err = check_range(lba, count);

    for (int tries = 0; (err == SDCARD_OK) && (count > 0u); ) {
        uint32_t done;
        err = read_blocks(lba, pos, count, &done);
        lba += done;
        pos += done * SDCARD_SECTOR_SIZE;
        count -= done;
        if (done) {
            tries = 0;                  /* progress: start counting afresh */
        }
        if (((err == SDCARD_ERR_CRC) || (err == SDCARD_ERR_TIMEOUT)) &&
            (++tries < SDCARD_SPI_RETRIES)) {
            err = SDCARD_OK;
        }
    }
    return err;
}

/* One attempt at writing count sectors. */
static int write_blocks(uint32_t lba, const uint8_t *pos, uint32_t count)
{
    int err = SDCARD_OK;

    cur_lba = lba;
    if (count == 1u) {
        err = (sd_cmd(CMD24, card_addr(lba)) == 0) ? tx_block(pos, TOKEN_START)
                                                 : fail(SDCARD_ERR_CMD, 0xFFu);
    } else {
        if (info.type != SDCARD_TYPE_MMC) {
            sd_cmd(ACMD23, count);      /* pre-erase hint, speeds up the write */
        }
        if (sd_cmd(CMD25, card_addr(lba)) != 0) {
            err = fail(SDCARD_ERR_CMD, 0xFFu);
        } else {
            for (uint32_t block = 0; (err == SDCARD_OK) && (block < count); block++) {
                cur_lba = lba + block;
                err = tx_block(pos, TOKEN_START_MULTI);
                pos += SDCARD_SECTOR_SIZE;
            }
            int stop_err = stop_write();       /* also after an error, see stop_write() */
            if (err == SDCARD_OK) {
                err = stop_err;
            }
        }
    }
    /* Wait for programming to finish so that errors show up here, and
     * check the card status: a block can be accepted and still fail. */
    if ((err == SDCARD_OK) && !wait_ready(TMO_WRITE_MS)) {
        err = fail(SDCARD_ERR_TIMEOUT, 0x00u);
    }
    if (err == SDCARD_OK) {
        uint8_t r1 = sd_cmd(CMD13, 0);
        uint8_t r2 = rx_byte();
        /* A multiple-block write that ends on the last sector may set
         * OUT_OF_RANGE although it was correct; the spec says to ignore it. */
        if ((count > 1u) && ((lba + count) == info.sectors)) {
            r2 &= (uint8_t)~R2_OUT_OF_RANGE;
        }
        if ((r1 != 0) || (r2 != 0)) {
            err = fail(SDCARD_ERR_WRITE, ((uint32_t)r1 << 8) | r2);
        }
    }
    deselect();
    return err;
}

int sdcard_write(uint32_t lba, const void *buf, uint32_t count)
{
    int err = check_range(lba, count);

    /* After a timeout or a CRC error, write the whole range again: writing
     * the same data twice is harmless, and the card does not tell (in SPI
     * mode, without ACMD22) which of the blocks before the error it kept. */
    for (int tries = 1; err == SDCARD_OK; tries++) {
        err = write_blocks(lba, (const uint8_t *)buf, count);
        if ((err != SDCARD_ERR_TIMEOUT) && (err != SDCARD_ERR_CRC)) {
            break;
        }
        if (tries >= SDCARD_SPI_RETRIES) {
            break;
        }
        err = SDCARD_OK;
    }
    return err;
}

int sdcard_sync(void)
{
    if (!ready) {
        return SDCARD_ERR_PARAM;
    }
    int ok = select_card();
    deselect();
    return ok ? SDCARD_OK : fail(SDCARD_ERR_TIMEOUT, 0x00u);
}
