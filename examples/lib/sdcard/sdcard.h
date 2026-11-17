/*
 * sdcard.h - SD / MMC card block access, independent of the bus.
 *
 *   if (sdcard_init() == SDCARD_OK) {
 *       const sdcard_info_t *ci = sdcard_get_info();
 *       printf("%s, %lu sectors\n", sdcard_type_name(ci->type),
 *              (unsigned long)ci->sectors);
 *       sdcard_read(0, buf, 1);            // sector 0 (MBR), 512 bytes
 *   }
 *
 * Exactly one backend implements these functions, chosen at link time:
 *
 *   sdcard_spi.c    SPI mode, on the LSPI (include sdcard_spi.mk)
 *   (an SDIO backend will implement the same API)
 *
 * sdcard.c holds the parts every backend shares: the CRC7 / CRC16 used
 * on the wire and the CSD / CID decoding. lib/fatfs/diskio_sdcard.c
 * puts FatFs on top of this API, so FatFs works with either backend.
 *
 * Sectors are always 512 bytes and numbered from 0, also on old
 * byte-addressed cards (the backend converts). Buffers need no alignment.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDCARD_SECTOR_SIZE  512u

/* Results. Negative values are errors. */
typedef enum {
    SDCARD_OK            =  0,
    SDCARD_ERR_NOCARD    = -1,  /* no answer to CMD0 / card detect says empty */
    SDCARD_ERR_TIMEOUT   = -2,  /* card stayed busy or sent no data token */
    SDCARD_ERR_CRC       = -3,  /* CRC error on received data, or card rejected ours */
    SDCARD_ERR_WRITE     = -4,  /* card rejected a written block */
    SDCARD_ERR_UNUSABLE  = -5,  /* voltage range or card type not supported */
    SDCARD_ERR_CMD       = -6,  /* card answered a command with an error */
    SDCARD_ERR_PARAM     = -7,  /* sector out of range, or not initialized */
} sdcard_err_t;

typedef enum {
    SDCARD_TYPE_NONE = 0,
    SDCARD_TYPE_MMC,            /* MMC v3 */
    SDCARD_TYPE_SDV1,           /* SD v1.x, byte addressed */
    SDCARD_TYPE_SDSC,           /* SD v2+, standard capacity (<= 2 GB), byte addressed */
    SDCARD_TYPE_SDHC,           /* SD v2+, high/extended capacity (SDHC/SDXC), block addressed */
} sdcard_type_t;

typedef struct {
    sdcard_type_t type;
    uint32_t sectors;           /* capacity in 512-byte sectors */
    uint32_t erase_sectors;     /* erase block / allocation unit in sectors (1 if unknown) */
    uint32_t ocr;
    uint32_t clock_hz;          /* bus clock in use */
    uint8_t  csd[16];           /* raw registers, MSB first as sent by the card */
    uint8_t  cid[16];
} sdcard_info_t;

/* CID fields (SD layout; MMC differs, see sdcard_parse_cid()). */
typedef struct {
    uint8_t  mid;               /* manufacturer ID */
    char     oid[3];            /* OEM ID, 2 chars + NUL */
    char     pnm[6];            /* product name, 5 chars + NUL */
    uint8_t  prv;               /* product revision, BCD n.m */
    uint32_t psn;               /* serial number */
    uint16_t year;              /* manufacturing date */
    uint8_t  month;
} sdcard_cid_t;

/* The last error in detail, for debugging (sdcard_get_diag()). */
typedef struct {
    int      err;               /* sdcard_err_t, SDCARD_OK if none since sdcard_init() */
    uint8_t  cmd;               /* command it happened in (ACMDn: 0x80 | n) */
    uint8_t  r1;                /* the card's response to it (0xFF: none) */
    uint32_t resp;              /* backend detail. SPI: data token, data response
                                   (xxx0sss1), received CRC, or R1 << 8 | R2 of CMD13 */
    uint32_t lba;               /* sector being transferred */
    uint32_t count;             /* errors since sdcard_init(), retried ones included */
} sdcard_diag_t;

/* ---- implemented by the backend ------------------------------------------ */

/* Set up the bus and bring the card to transfer state. Can be called again
 * (e.g. after a card swap). */
int sdcard_init(void);

/* 1 once sdcard_init() has succeeded (0 again if a later init fails). */
int sdcard_ready(void);

/* 1 if a card is in the slot: always 1 without a card-detect pin. */
int sdcard_present(void);

/* count sectors from sector lba. Return SDCARD_OK or an error. */
int sdcard_read(uint32_t lba, void *buf, uint32_t count);
int sdcard_write(uint32_t lba, const void *buf, uint32_t count);

/* Wait until the card has finished programming. */
int sdcard_sync(void);

/* Card details from the last successful sdcard_init(). */
const sdcard_info_t *sdcard_get_info(void);

/* Details of the last error. */
const sdcard_diag_t *sdcard_get_diag(void);

/* Change the bus clock after sdcard_init() (at most the backend's limit).
 * Returns the rate set, also stored in sdcard_get_info()->clock_hz. */
uint32_t sdcard_set_clock(uint32_t hz);

/* ---- shared helpers (sdcard.c) -------------------------------------------- */

uint8_t  sdcard_crc7(const uint8_t *data, size_t len);              /* command CRC (7 bits) */
uint16_t sdcard_crc16(uint16_t crc, const void *data, size_t len);  /* data CRC, start with 0 */

/* Capacity in sectors from a CSD. SD: CSD versions 1.0, 2.0 and 3.0 (3.0
 * is clamped to 32 bits). MMC: the CSD C_SIZE only, cards up to 2 GB. */
uint32_t sdcard_csd_sectors(const uint8_t csd[16], sdcard_type_t type);

/* Erase unit from a CSD in sectors, 1 if the CSD does not give one (SD
 * CSD 2.0: the allocation unit is in the SD status instead). */
uint32_t sdcard_csd_erase_sectors(const uint8_t csd[16], sdcard_type_t type);

/* Decode a CID. MMC: oid is left empty and pnm keeps the first 5 of the
 * 6 name characters. */
void sdcard_parse_cid(const uint8_t cid[16], sdcard_type_t type, sdcard_cid_t *out);

const char *sdcard_type_name(sdcard_type_t type);
const char *sdcard_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* SDCARD_H */
