/*
 * sd_fatfs - FatFs on an SD card over SPI: card info, raw read speed,
 * directory listing, file write/read/verify with throughput, long file
 * names, rename/delete, and a boot counter kept in a file.
 *
 * Board: HLK-W801-KIT-V1.1. Output on UART0, 115200 baud (`make monitor`).
 *
 * WIRING (an SD card breakout or a micro-SD socket, 3.3 V card supply):
 *
 *   card SCK  -> PB2     card MISO -> PB3     card MOSI -> PA7
 *   card CS   -> PB4     VDD -> 3.3 V         GND -> GND
 *
 * Modules with a 5 V regulator and level shifter work from 3.3 V on VCC
 * only if the regulator is low-dropout; otherwise power them from 5 V.
 * Other pins and the SPI clock: see examples/lib/sdcard/sdcard_spi.c, e.g.
 *   make CFLAGS=-DSDCARD_SPI_HZ=8000000
 *
 * The card must hold a FAT12/16/32 or exFAT file system (a new card
 * does). This program only adds and removes files in /w801test, apart from
 * the boot counter file it keeps there. The raw write test rewrites the
 * last 16 sectors of the card with the data they already hold. To format a card that has no
 * file system, build with `make CFLAGS=-DSD_FORMAT=1`: that ERASES the card.
 *
 * LED1 lights if everything passed, LED2 if anything failed.
 */
#include <stdio.h>
#include <string.h>
#include "w80x.h"
#include "w80x_clock.h"
#include "w80x_gpio.h"
#include "w80x_uart.h"
#include "w80x_timer.h"
#include "w80x_utils.h"
#include "boards/hlk_w801_kit.h"
#include "sdcard.h"
#include "ff.h"

#ifndef SD_FORMAT
#define SD_FORMAT 0
#endif

#if defined(SDCARD_HW_CRC) && SDCARD_HW_CRC
#define SDCARD_HW_CRC_NAME "hardware (crypto engine, SDCARD_HW_CRC=1)"
#else
#define SDCARD_HW_CRC_NAME "software (table)"
#endif

#define TEST_DIR    "/w801test"
#define DATA_FILE   TEST_DIR "/data.bin"
#define DATA_SIZE   (1024u * 1024u)
#define LFN_FILE    TEST_DIR "/A long file name, \xC3\xBCnicode.txt"   /* "ü" in UTF-8 */
#define LFN_RENAMED TEST_DIR "/renamed long name.txt"
#define BOOT_FILE   TEST_DIR "/boot.log"

static FATFS fs;
static FIL fil;
static uint8_t buf[16 * SDCARD_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t chk[16 * SDCARD_SECTOR_SIZE] __attribute__((aligned(4)));
static int fails;
static volatile uint32_t ms_ticks;

W80X_IRQ void TIMER_IRQHandler(void)
{
    uint32_t fired = timer_irq_status();
    timer_irq_clear(fired);
    if (fired & BIT(TIMER0)) {
        ms_ticks++;
    }
}

static const char *fr_str(FRESULT r)
{
    static const char *const s[] = {
        "OK", "DISK_ERR", "INT_ERR", "NOT_READY", "NO_FILE", "NO_PATH",
        "INVALID_NAME", "DENIED", "EXIST", "INVALID_OBJECT", "WRITE_PROTECTED",
        "INVALID_DRIVE", "NOT_ENABLED", "NO_FILESYSTEM", "MKFS_ABORTED",
        "TIMEOUT", "LOCKED", "NOT_ENOUGH_CORE", "TOO_MANY_OPEN_FILES",
        "INVALID_PARAMETER",
    };
    return ((unsigned)r < (sizeof s / sizeof s[0])) ? s[r] : "?";
}

static void check_ok(const char *name, int ok)
{
    printf("%-40s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) {
        fails++;
    }
}

/* What the SD layer saw last, if anything went wrong. */
static void print_diag(void)
{
    const sdcard_diag_t *d = sdcard_get_diag();

    if (d->err != SDCARD_OK) {
        printf("    sdcard: %s in %s%u (R1 %02x, resp %04lx) at sector %lu; %lu errors so far\n",
               sdcard_strerror(d->err), (d->cmd & 0x80u) ? "ACMD" : "CMD", d->cmd & 0x7Fu,
               d->r1, (unsigned long)d->resp, (unsigned long)d->lba, (unsigned long)d->count);
    }
}

/* PASS if r is FR_OK, else FAIL with the FatFs result. Returns 1 for FR_OK. */
static int check_fr(const char *name, FRESULT r)
{
    if (r != FR_OK) {
        printf("    %s\n", fr_str(r));
        if ((r == FR_DISK_ERR) || (r == FR_NOT_READY)) {
            print_diag();
        }
    }
    check_ok(name, r == FR_OK);
    return r == FR_OK;
}

static void print_rate(uint32_t bytes, uint32_t ms)
{
    if (ms == 0u) {
        ms = 1u;
    }
    printf("    %lu KB in %lu ms = %lu KB/s\n", (unsigned long)(bytes / 1024u),
           (unsigned long)ms, (unsigned long)((bytes / 1024u) * 1000u / ms));
}

/* Test pattern: 32-bit words that depend on the byte offset in the file. */
static void fill_pattern(uint8_t *p, uint32_t len, uint32_t offset)
{
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t v = ((offset + i) * 2654435761u) ^ 0xA5A5A5A5u;
        memcpy(p + i, &v, 4);
    }
}

static int check_pattern(const uint8_t *p, uint32_t len, uint32_t offset)
{
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t v = ((offset + i) * 2654435761u) ^ 0xA5A5A5A5u;
        if (memcmp(p + i, &v, 4) != 0) {
            printf("    mismatch at offset %lu\n", (unsigned long)(offset + i));
            return 0;
        }
    }
    return 1;
}

static void card_info(void)
{
    const sdcard_info_t *ci = sdcard_get_info();
    sdcard_cid_t cid;

    sdcard_parse_cid(ci->cid, ci->type, &cid);
    printf("card: %s, %lu MB (%lu sectors), erase unit %lu KB\n",
           sdcard_type_name(ci->type), (unsigned long)(ci->sectors / 2048u),
           (unsigned long)ci->sectors, (unsigned long)(ci->erase_sectors / 2u));
    printf("      maker 0x%02x \"%s\", product \"%s\" rev %u.%u, serial %08lx, made %u/%02u\n",
           cid.mid, cid.oid, cid.pnm, cid.prv >> 4, cid.prv & 0x0Fu,
           (unsigned long)cid.psn, cid.year, cid.month);
    printf("      OCR %08lx, SPI clock %lu kHz\n\n", (unsigned long)ci->ocr,
           (unsigned long)(ci->clock_hz / 1000u));
}

/* The data CRC against the standard check value (from flash and from RAM),
 * and its cost per 512-byte block. Build with CFLAGS=-DSDCARD_HW_CRC=1 to
 * compare the crypto engine with the software table. */
static void crc_test(void)
{
    static const char check[] = "123456789";       /* const: in flash */
    char ram[sizeof check];

    memcpy(ram, check, sizeof check);
    check_ok("CRC16 check value (flash, RAM)",
             (sdcard_crc16(0, check, 9) == 0x31C3u) && (sdcard_crc16(0, ram, 9) == 0x31C3u));

    memset(buf, 0xFF, SDCARD_SECTOR_SIZE);
    check_ok("CRC16 of an erased block", sdcard_crc16(0, buf, SDCARD_SECTOR_SIZE) == 0x7FA1u);

    uint32_t t0 = CPU_TICKS;
    for (int i = 0; i < 16; i++) {
        sdcard_crc16(0, buf, SDCARD_SECTOR_SIZE);
    }
    uint32_t cyc = cpu_cycles_since(t0) / 16u;
    printf("    %s: %lu cycles = %lu us per 512-byte block\n",
           SDCARD_HW_CRC_NAME, (unsigned long)cyc, (unsigned long)cpu_cycles_to_us(cyc));
}

/* Read the first 2048 sectors (1 MB) straight from the card. Read-only. */
static void raw_read_test(void)
{
    const sdcard_info_t *ci = sdcard_get_info();
    uint32_t n = (ci->sectors < 2048u) ? ci->sectors : 2048u;
    uint32_t per = sizeof buf / SDCARD_SECTOR_SIZE;
    int r = sdcard_read(0, buf, 1);

    check_ok("read sector 0", r == SDCARD_OK);
    if (r == SDCARD_OK) {
        printf("    signature %02x %02x (55 aa = MBR or boot sector)\n", buf[510], buf[511]);
    }

    uint32_t t0 = ms_ticks;
    for (uint32_t s = 0; (s < n) && (r == SDCARD_OK); s += per) {
        r = sdcard_read(s, buf, ((n - s) < per) ? (n - s) : per);
    }
    uint32_t ms = ms_ticks - t0;
    if (r != SDCARD_OK) {
        print_diag();
    }
    check_ok("raw read, 8 KB multi-block", r == SDCARD_OK);
    print_rate(n * SDCARD_SECTOR_SIZE, ms);
}

/* Read the last 16 sectors, write them back (one single-block and one
 * 15-block write), read them again and compare. Changes nothing on the card. */
static int rewrite_once(void)
{
    const sdcard_info_t *ci = sdcard_get_info();
    uint32_t n = sizeof buf / SDCARD_SECTOR_SIZE;
    uint32_t lba = ci->sectors - n;

    int r = sdcard_read(lba, buf, n);
    if (r == SDCARD_OK) {
        r = sdcard_write(lba, buf, 1);
    }
    if (r == SDCARD_OK) {
        r = sdcard_write(lba + 1u, buf + SDCARD_SECTOR_SIZE, n - 1u);
    }
    if (r == SDCARD_OK) {
        r = sdcard_read(lba, chk, n);
    }
    if (r != SDCARD_OK) {
        print_diag();
        return 0;
    }
    if (memcmp(buf, chk, sizeof buf) != 0) {
        printf("    read back differs from what was written\n");
        return 0;
    }
    return 1;
}

/* Raw writes at the full clock; on a failure, find a clock that works and
 * keep it, to tell a protocol problem from a wiring one. */
static void raw_write_test(void)
{
    static const uint32_t rates[] = { 20000000, 10000000, 4000000, 1000000 };
    uint32_t full = sdcard_get_info()->clock_hz;
    char name[48];

    for (uint32_t i = 0; i < (sizeof rates / sizeof rates[0]); i++) {
        if (rates[i] > full) {
            continue;
        }
        uint32_t hz = sdcard_set_clock(rates[i]);
        snprintf(name, sizeof name, "raw rewrite, SCK %lu kHz", (unsigned long)(hz / 1000u));
        int ok = rewrite_once();
        printf("%-40s %s\n", name, ok ? "PASS" : "FAIL");
        if (ok) {
            if (hz != full) {
                printf("    writes fail above this clock: check the wiring (short wires, "
                       "common GND); continuing at %lu kHz\n", (unsigned long)(hz / 1000u));
                fails++;
            }
            return;
        }
    }
    fails++;
    sdcard_set_clock(full);
    printf("    writes fail at every clock\n");
}

static int mount(void)
{
    FRESULT r = f_mount(&fs, "", 1);

    if ((r == FR_NO_FILESYSTEM) && SD_FORMAT) {
        static const MKFS_PARM opt = { FM_ANY, 0, 0, 0, 0 };
        printf("no file system: formatting (SD_FORMAT=1)...\n");
        r = f_mkfs("", &opt, buf, sizeof buf);
        check_fr("f_mkfs", r);
        if (r == FR_OK) {
            r = f_mount(&fs, "", 1);
        }
    } else if (r == FR_NO_FILESYSTEM) {
        printf("    no FAT/exFAT file system on the card; build with "
               "CFLAGS=-DSD_FORMAT=1 to format it (erases the card)\n");
    }
    return check_fr("f_mount", r);
}

static void volume_info(void)
{
    static const char *const types[] = { "?", "FAT12", "FAT16", "FAT32", "exFAT" };
    FATFS *pfs;
    DWORD free_clust;
    char label[34];
    DWORD vsn = 0;

    label[0] = '\0';
    f_getlabel("", label, &vsn);
    printf("volume: %s, label \"%s\", serial %08lx, cluster %lu KB\n",
           types[(fs.fs_type <= 4u) ? fs.fs_type : 0], label, (unsigned long)vsn,
           (unsigned long)(fs.csize / 2u));
    if (check_fr("f_getfree", f_getfree("", &free_clust, &pfs))) {
        /* clusters * sectors per cluster / 2048 = MB */
        unsigned long total = (unsigned long)(((uint64_t)(pfs->n_fatent - 2u) * pfs->csize) / 2048u);
        unsigned long avail = (unsigned long)(((uint64_t)free_clust * pfs->csize) / 2048u);
        printf("    %lu MB free of %lu MB\n", avail, total);
    }
}

static void list_root(void)
{
    DIR dir;
    FILINFO fno;
    int n = 0;

    FRESULT r = f_opendir(&dir, "/");
    if (r == FR_OK) {
        printf("root directory:\n");
        while (((r = f_readdir(&dir, &fno)) == FR_OK) && fno.fname[0]) {
            if (fno.fattrib & AM_DIR) {
                printf("    %-32s  <dir>\n", fno.fname);
            } else {
                printf("    %-32s  %10lu\n", fno.fname, (unsigned long)fno.fsize);
            }
            if (++n >= 30) {
                printf("    ...\n");
                break;
            }
        }
        f_closedir(&dir);
    }
    check_fr("list /", r);
}

/* Write DATA_SIZE bytes, read them back, check, seek, delete. */
static void file_test(void)
{
    UINT bw, br;
    FRESULT r = f_open(&fil, DATA_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    uint32_t t0, ms;

    if (!check_fr("create " DATA_FILE, r)) {
        return;
    }
    t0 = ms_ticks;
    for (uint32_t off = 0; (off < DATA_SIZE) && (r == FR_OK); off += sizeof buf) {
        fill_pattern(buf, sizeof buf, off);
        r = f_write(&fil, buf, sizeof buf, &bw);
        if ((r == FR_OK) && (bw != sizeof buf)) {
            printf("    disk full\n");
            r = FR_DENIED;
        }
    }
    if (r == FR_OK) {
        r = f_close(&fil);
    } else {
        f_close(&fil);
    }
    ms = ms_ticks - t0;
    if (!check_fr("write 1 MB in 8 KB pieces", r)) {
        f_unlink(DATA_FILE);
        return;
    }
    print_rate(DATA_SIZE, ms);

    FILINFO fno;
    r = f_stat(DATA_FILE, &fno);
    check_ok("f_stat size", (r == FR_OK) && (fno.fsize == DATA_SIZE));

    int ok = 1;
    r = f_open(&fil, DATA_FILE, FA_READ);
    t0 = ms_ticks;
    for (uint32_t off = 0; (off < DATA_SIZE) && (r == FR_OK) && ok; off += sizeof buf) {
        r = f_read(&fil, buf, sizeof buf, &br);
        ok = (r == FR_OK) && (br == sizeof buf) && check_pattern(buf, sizeof buf, off);
    }
    ms = ms_ticks - t0;
    check_fr("read back", r);
    check_ok("data matches", ok);
    print_rate(DATA_SIZE, ms);

    /* unaligned seeks and short reads */
    static const uint32_t offs[] = { 4, 511 * 4, 12345 * 4, (DATA_SIZE / 2u) + 4u, DATA_SIZE - 100u };
    ok = (r == FR_OK);
    for (uint32_t i = 0; ok && (i < (sizeof offs / sizeof offs[0])); i++) {
        ok = (f_lseek(&fil, offs[i]) == FR_OK) &&
             (f_read(&fil, buf, 100, &br) == FR_OK) && (br == 100u) &&
             check_pattern(buf, 100, offs[i]);
    }
    check_ok("f_lseek + short reads", ok);
    f_close(&fil);

    check_fr("delete " DATA_FILE, f_unlink(DATA_FILE));
}

/* Long name with a non-ASCII character, string functions, rename. */
static void lfn_test(void)
{
    char line[64];
    UINT n = 0;

    FRESULT r = f_open(&fil, LFN_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (r == FR_OK) {
        f_printf(&fil, "hello from the W80x, %d %s\n", 42, "fatfs");
        r = f_close(&fil);
    }
    check_fr("create long file name", r);

    line[0] = '\0';
    r = f_open(&fil, LFN_FILE, FA_READ);
    if (r == FR_OK) {
        f_gets(line, sizeof line, &fil);
        f_close(&fil);
    }
    check_ok("f_printf / f_gets", (r == FR_OK) && (strcmp(line, "hello from the W80x, 42 fatfs\n") == 0));

    f_unlink(LFN_RENAMED);
    check_fr("f_rename", f_rename(LFN_FILE, LFN_RENAMED));

    DIR dir;
    FILINFO fno;
    if (f_findfirst(&dir, &fno, TEST_DIR, "renamed*") == FR_OK) {
        n = fno.fname[0] ? 1u : 0u;
        f_closedir(&dir);
    }
    check_ok("f_findfirst finds it", n == 1u);
    check_fr("delete it", f_unlink(LFN_RENAMED));
}

/* Append one line per boot; the count survives power cycles. */
static void boot_counter(void)
{
    char line[48];
    unsigned boots = 0;

    FRESULT r = f_open(&fil, BOOT_FILE, FA_OPEN_ALWAYS | FA_READ | FA_WRITE);
    if (r == FR_OK) {
        while (f_gets(line, sizeof line, &fil)) {
            boots++;
        }
        /* f_gets left the pointer at the end */
        f_printf(&fil, "boot %u, %lu ms after reset\n", boots + 1u, (unsigned long)ms_ticks);
        r = f_close(&fil);
    }
    if (check_fr("append to " BOOT_FILE, r)) {
        printf("    this is boot %u with this card\n", boots + 1u);
    }
}

int main(void)
{
    clock_set_cpu_div(CLK_CPU_240MHZ);
    led_init(LED1);
    led_init(LED2);
    uart0_init(115200);
    stdio_set_uart(UART0);

    timer_init();
    timer_start(TIMER0, 1, TIMER_MS | TIMER_PERIODIC | TIMER_IRQ);
    irq_enable(TIMER_IRQn);
    __enable_irq();

    printf("\n\nSD card + FatFs R0.16 over SPI on the %s, CPU %lu MHz\n",
           BOARD_NAME, (unsigned long)(clock_cpu_hz() / 1000000u));
    printf("Card: SCK PB2, MISO PB3, MOSI PA7, CS PB4\n");
    printf("Data CRC: %s\n\n", SDCARD_HW_CRC_NAME);

    int r = sdcard_init();
    if (r != SDCARD_OK) {
        print_diag();
    }
    check_ok("sdcard_init", r == SDCARD_OK);

    crc_test();
    if (r == SDCARD_OK) {
        card_info();
        raw_read_test();
        raw_write_test();
        if (mount()) {
            volume_info();
            list_root();
            FRESULT d = f_mkdir(TEST_DIR);
            if (check_fr("mkdir " TEST_DIR, (d == FR_EXIST) ? FR_OK : d)) {
                file_test();
                lfn_test();
                boot_counter();
            }
            check_fr("f_unmount", f_mount(NULL, "", 0));
        }
    }

    if (sdcard_get_diag()->count) {
        printf("\nSD errors seen, retried ones included: %lu. The last one:\n",
               (unsigned long)sdcard_get_diag()->count);
        print_diag();
    }
    printf("\n%d failed\n", fails);
    led_on(fails ? LED2 : LED1);
    for (;;) {
        __wait();
    }
}
