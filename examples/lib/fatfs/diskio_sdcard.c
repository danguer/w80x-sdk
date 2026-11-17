/*
 * diskio_sdcard.c - FatFs disk I/O layer on top of sdcard.h.
 *
 * Physical drive 0 is the SD card, through whichever sdcard backend is
 * linked (SPI now, SDIO later), so f_mount(&fs, "", 1) mounts it.
 *
 * get_fattime() is weak and stamps files with the RTC time (w80x_rtc.h), or
 * with the fixed date in ffconf.h (FF_NORTC_*) while the RTC is stopped.
 * Define your own to use another clock.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "ff.h"
#include "diskio.h"
#include "sdcard.h"
#include "w80x_rtc.h"

#define SD_DRIVE 0u

static DRESULT to_dresult(int r)
{
    if (r == SDCARD_OK) {
        return RES_OK;
    }
    if (r == SDCARD_ERR_PARAM) {
        return RES_PARERR;
    }
    return sdcard_ready() ? RES_ERROR : RES_NOTRDY;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != SD_DRIVE) {
        return STA_NOINIT;
    }
    if (!sdcard_present()) {
        return STA_NOINIT | STA_NODISK;
    }
    return sdcard_ready() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != SD_DRIVE) {
        return STA_NOINIT;
    }
    if (!sdcard_ready()) {
        sdcard_init();
    }
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SD_DRIVE) {
        return RES_PARERR;
    }
    return to_dresult(sdcard_read((uint32_t)sector, buff, count));
}

#if !FF_FS_READONLY
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SD_DRIVE) {
        return RES_PARERR;
    }
    return to_dresult(sdcard_write((uint32_t)sector, buff, count));
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    const sdcard_info_t *ci = sdcard_get_info();

    if (pdrv != SD_DRIVE) {
        return RES_PARERR;
    }
    if (!sdcard_ready()) {
        return RES_NOTRDY;
    }
    switch (cmd) {
    case CTRL_SYNC:
        return to_dresult(sdcard_sync());
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = ci->sectors;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SDCARD_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = ci->erase_sectors;
        return RES_OK;
    case MMC_GET_TYPE:
        *(BYTE *)buff = (BYTE)ci->type;
        return RES_OK;
    case MMC_GET_CSD:
        memcpy(buff, ci->csd, 16);
        return RES_OK;
    case MMC_GET_CID:
        memcpy(buff, ci->cid, 16);
        return RES_OK;
    case MMC_GET_OCR:
        *(DWORD *)buff = ci->ocr;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

#if !FF_FS_NORTC && !FF_FS_READONLY
/* bits 31-25 year - 1980, 24-21 month, 20-16 day, 15-11 hour, 10-5 min, 4-0 s / 2 */
__attribute__((weak)) DWORD get_fattime(void)
{
    struct tm t;

    if (!rtc_is_running()) {
        return ((DWORD)(FF_NORTC_YEAR - 1980) << 25) | ((DWORD)FF_NORTC_MON << 21) |
               ((DWORD)FF_NORTC_MDAY << 16);
    }
    rtc_get_time(&t);
    /* FAT dates end at 2107; the RTC goes to 2127 */
    if (t.tm_year > (2107 - 1900)) {
        t.tm_year = 2107 - 1900;
    }
    return ((DWORD)(t.tm_year - 80) << 25) | ((DWORD)(t.tm_mon + 1) << 21) |
           ((DWORD)t.tm_mday << 16) | ((DWORD)t.tm_hour << 11) |
           ((DWORD)t.tm_min << 5) | ((DWORD)t.tm_sec >> 1);
}
#endif
