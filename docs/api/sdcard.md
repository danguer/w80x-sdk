# SD card and FatFs

[API index](README.md)

- **Source:** [`examples/lib/sdcard`](../../examples/lib/sdcard/), [`examples/lib/fatfs`](../../examples/lib/fatfs/)
- **Example:** [`examples/sd_fatfs`](../../examples/sd_fatfs/main.c)

```make
# project Makefile
SRCS := main.c
include ../lib/fatfs/fatfs.mk
include ../lib/sdcard/sdcard_spi.mk          # the SPI backend, or sdcard_sdio.mk
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
implements it on the LSPI and `sdcard_sdio.c` on the SDIO host (SD bus
mode, see below); a project links one of them. FatFs sits on top through `diskio_sdcard.c`, so FatFs code does not
change when the backend does. The SPI backend handles SDHC/SDXC, SDSC, SD v1
and MMC. It runs at 400 kHz for init and 20 MHz after that. CRC checking is
on (CMD59 for the card's side, CRC16 on every block read); build with
`CFLAGS=-DSDCARD_HW_CRC=1` to compute those CRCs on the crypto engine instead
of a table in software. Build with `CFLAGS=-DSDCARD_SPI_DMA=1` to move the
512-byte data blocks with DMA (channels 6 and 7, set with
`SDCARD_SPI_DMA_TX_CH` / `_RX_CH`) instead of through the SPI FIFO; buffers
that are not word-aligned SRAM still go through the FIFO. Both options can
be combined. DMA alone gains about 10-30 % through FatFs (measured: 464
KB/s write, 593 KB/s read), because the software CRC takes about 520 µs per
block, longer than the 205 µs the block spends on the wire. Failed reads
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

## SDIO backend

`sdcard_sdio.c` drives the card in SD bus mode through the SDIO host
([`w80x_sdh.h`](sdh.md)): 4 data lines (or 1, `-DSDCARD_SDIO_WIDTH=1`), 20 MHz
by default (`SDCARD_SDIO_HZ`, at most 25 MHz), CRCs computed by the controller.
Default pins, all AF1: CLK PA9, CMD PA10, DAT0..DAT3 PA11..PA14 (each one
a `-D` option, as for SPI). Use a plain micro-SD socket: SPI breakouts with a
level shifter pass DAT0 one way only and leave DAT1/DAT2 unconnected.

Several sectors in one call go as one multi-block transfer by DMA (channel
`SDCARD_SDIO_DMA_CH`, default 5, up to 127 blocks) when the buffer is
word-aligned SRAM; single sectors and other buffers go one block at a time
through the controller's 512-byte buffer. It handles SD v1, SDSC and
SDHC/SDXC cards, not MMC. `sdcard_bus_info()` returns the bus and pins as
text. In `examples/sd_fatfs`, `make SDCARD_BUS=sdio` builds with it.

Not yet run on hardware.
