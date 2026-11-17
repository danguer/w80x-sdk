# SD card and FatFs

[API index](README.md)

- **Source:** [`examples/lib/sdcard`](../../examples/lib/sdcard/), [`examples/lib/fatfs`](../../examples/lib/fatfs/)
- **Example:** [`examples/sd_fatfs`](../../examples/sd_fatfs/main.c)

```make
# project Makefile
SRCS := main.c
include ../lib/fatfs/fatfs.mk
include ../lib/sdcard/sdcard_spi.mk          # the SPI backend
include ../../w80x/w80x.mk
```

```c
sdcard_init();                                // SCK PB2, MISO PB3, MOSI PA7, CS PB4 by default
sdcard_read(lba, buf, n);                     // raw 512-byte sectors, if you want them

FATFS fs; FIL f; UINT bw;
f_mount(&fs, "", 1);
f_open(&f, "/log.txt", FA_OPEN_APPEND | FA_WRITE);
f_write(&f, "hi\n", 3, &bw);
f_close(&f);
```

`sdcard.h` is a block API that does not depend on the bus: `sdcard_spi.c`
implements it on the LSPI, and an SDIO backend can implement the same functions
later. FatFs sits on top through `diskio_sdcard.c`, so FatFs code does not
change when the backend does. The SPI backend handles SDHC/SDXC, SDSC, SD v1
and MMC. It runs at 400 kHz for init and 20 MHz after that. CRC checking is
on (CMD59 for the card's side, CRC16 on every block read); build with
`CFLAGS=-DSDCARD_HW_CRC=1` to compute those CRCs on the crypto engine instead
of a table in software. Failed reads
and writes are retried; writes wait up to 2 s for a busy card, since cheap
cards can exceed the spec's 500 ms. `sdcard_get_diag()` tells the command,
response and sector of the last error. Pins and clock are `-D` options, listed at the top of
`sdcard_spi.c`.

FatFs is the unmodified R0.16 release. `ffconf.h` lists the settings changed
from stock at its top: long file names with a UTF-8 API, exFAT, `f_mkfs`,
`f_printf`/`f_gets`, `f_findfirst` and relative paths. Unused functions are
removed at link time. `examples/sd_fatfs` comes to about 64 KB of flash and
13 KB of RAM. `get_fattime()` is weak and stamps files with the
RTC time ([RTC](rtc.md)), or with a fixed date (`FF_NORTC_*`) while the RTC
is stopped; define your own to use another clock.
