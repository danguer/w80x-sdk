/*
 * sdcard.c - Parts of the SD card layer shared by every backend: wire CRCs,
 * CSD and CID decoding, names. See sdcard.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "sdcard.h"

/* SDCARD_HW_CRC=1: data CRCs on the GPSEC crypto engine instead of the
 * table below (saves its 512 bytes of flash). The engine is shared with
 * w80x_crypto.h: don't use CRC/crypto functions from an interrupt handler
 * while card transfers can run, and vice versa. */
#ifndef SDCARD_HW_CRC
#define SDCARD_HW_CRC 0
#endif

#if SDCARD_HW_CRC
#include <string.h>
#include "w80x_crypto.h"
#endif

/* CRC7, polynomial x^7 + x^3 + 1, as used on commands. Returns the 7-bit
 * value; the command byte is (crc << 1) | 1. */
uint8_t sdcard_crc7(const uint8_t *p, size_t len)
{
    uint8_t crc = 0;

    while (len--) {
        uint8_t d = *p++;
        for (int i = 0; i < 8; i++) {
            crc <<= 1;
            if ((d ^ crc) & 0x80u) {
                crc ^= 0x09u;
            }
            d <<= 1;
        }
    }
    return crc & 0x7Fu;
}

/* CRC16-CCITT (XMODEM: poly 0x1021, init 0, no reflection), as used on
 * data blocks. Check value: "123456789" -> 0x31C3. */
#if SDCARD_HW_CRC

uint16_t sdcard_crc16(uint16_t crc, const void *p, size_t len)
{
    /* Clock on and reset the engine the first time only, so a TRNG or other
     * setup the program made is left alone afterwards. */
    if (!(CLK->GATE_EN & CLK_GATE_GPSEC)) {
        crypto_init();
    }
    return (uint16_t)crc_calc(CRC_16_CCITT, crc, p, len);
}

#else

static const uint16_t crc16_tab[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
};

uint16_t sdcard_crc16(uint16_t crc, const void *p, size_t len)
{
    const uint8_t *b = (const uint8_t *)p;

    while (len--) {
        crc = (uint16_t)((crc << 8) ^ crc16_tab[((crc >> 8) ^ *b++) & 0xFFu]);
    }
    return crc;
}

#endif /* SDCARD_HW_CRC */

/* Bits [msb:lsb] of a 128-bit register sent MSB first (r[0] = bits 127..120). */
static uint32_t bits(const uint8_t r[16], uint32_t msb, uint32_t lsb)
{
    uint32_t v = 0;

    for (uint32_t i = msb + 1u; i-- > lsb; ) {
        v = (v << 1) | ((r[15u - (i / 8u)] >> (i % 8u)) & 1u);
    }
    return v;
}

uint32_t sdcard_csd_sectors(const uint8_t csd[16], sdcard_type_t type)
{
    uint32_t ver = bits(csd, 127, 126);

    if ((type != SDCARD_TYPE_MMC) && (ver == 1u)) {          /* CSD 2.0: SDHC/SDXC */
        return (bits(csd, 69, 48) + 1u) << 10;
    }
    if ((type != SDCARD_TYPE_MMC) && (ver == 2u)) {          /* CSD 3.0: SDUC */
        uint64_t n = ((uint64_t)bits(csd, 75, 48) + 1u) << 10;
        return (n > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)n;
    }
    /* CSD 1.0 and MMC: (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) blocks of 2^READ_BL_LEN */
    uint32_t c_size = bits(csd, 73, 62);
    uint32_t mult   = bits(csd, 49, 47);
    uint32_t bl_len = bits(csd, 83, 80);
    uint32_t sh = mult + 2u + bl_len;
    return (sh >= 9u) ? ((c_size + 1u) << (sh - 9u)) : ((c_size + 1u) >> (9u - sh));
}

uint32_t sdcard_csd_erase_sectors(const uint8_t csd[16], sdcard_type_t type)
{
    uint32_t wbl = bits(csd, 25, 22);                       /* WRITE_BL_LEN */
    uint32_t n;

    if (type == SDCARD_TYPE_MMC) {
        /* (ERASE_GRP_SIZE + 1) * (ERASE_GRP_MULT + 1) write blocks */
        n = (bits(csd, 46, 42) + 1u) * (bits(csd, 41, 37) + 1u);
    } else if (bits(csd, 127, 126) == 0u) {
        n = bits(csd, 45, 39) + 1u;                         /* SECTOR_SIZE + 1 */
    } else {
        return 1u;
    }
    return (wbl > 9u) ? (n << (wbl - 9u)) : n;
}

void sdcard_parse_cid(const uint8_t cid[16], sdcard_type_t type, sdcard_cid_t *out)
{
    out->mid = cid[0];
    if (type == SDCARD_TYPE_MMC) {
        out->oid[0] = '\0';
        for (int i = 0; i < 5; i++) {
            out->pnm[i] = (char)cid[3 + i];
        }
        out->prv   = cid[9];
        out->psn   = ((uint32_t)cid[10] << 24) | ((uint32_t)cid[11] << 16) |
                     ((uint32_t)cid[12] << 8) | cid[13];
        out->month = cid[14] >> 4;
        out->year  = (uint16_t)(1997u + (cid[14] & 0x0Fu));
    } else {
        out->oid[0] = (char)cid[1];
        out->oid[1] = (char)cid[2];
        for (int i = 0; i < 5; i++) {
            out->pnm[i] = (char)cid[3 + i];
        }
        out->prv   = cid[8];
        out->psn   = ((uint32_t)cid[9] << 24) | ((uint32_t)cid[10] << 16) |
                     ((uint32_t)cid[11] << 8) | cid[12];
        out->year  = (uint16_t)(2000u + (((cid[13] & 0x0Fu) << 4) | (cid[14] >> 4)));
        out->month = cid[14] & 0x0Fu;
    }
    out->oid[2] = '\0';
    out->pnm[5] = '\0';
}

const char *sdcard_type_name(sdcard_type_t type)
{
    switch (type) {
    case SDCARD_TYPE_MMC:  return "MMC";
    case SDCARD_TYPE_SDV1: return "SD v1";
    case SDCARD_TYPE_SDSC: return "SDSC";
    case SDCARD_TYPE_SDHC: return "SDHC/SDXC";
    default:               return "none";
    }
}

const char *sdcard_strerror(int err)
{
    switch (err) {
    case SDCARD_OK:           return "ok";
    case SDCARD_ERR_NOCARD:   return "no card";
    case SDCARD_ERR_TIMEOUT:  return "timeout";
    case SDCARD_ERR_CRC:      return "CRC error";
    case SDCARD_ERR_WRITE:    return "write rejected";
    case SDCARD_ERR_UNUSABLE: return "unusable card";
    case SDCARD_ERR_CMD:      return "command error";
    case SDCARD_ERR_PARAM:    return "bad parameter";
    default:                  return "?";
    }
}
