# W80x SDK API

One page per part of the library. Each page has a short usage example and
the points to watch. The headers themselves have the full details, and the
[register chapters](../README.md) describe the hardware underneath.

All drivers are header-only unless noted. Include `w80x.h` and the header
for the part you use, and link with `w80x/w80x.mk`.

## Contents

### Core

| Page | Header | Covers |
|---|---|---|
| [System](system.md) | `w80x.h` | Interrupts and handlers, busy-wait delays, early init, `.noinit` RAM, fault hook |
| [Clocks](clock.md) | `w80x_clock.h` | CPU clock settings, bus clocks, peripheral clock gating and reset, JTAG |
| [Cycle counting](utils.md) | `w80x_utils.h` | Measuring time in CPU cycles, converting to µs/ms |
| [Watchdog](watchdog.md) | `w80x_wdg.h` | Start, feed, stop, software reset, reset cause |
| [Real-time clock (RTC)](rtc.md) | `w80x_rtc.h` | `struct tm` / `time_t` time, `time()`, alarm, 32 kHz clock source |

### Peripherals

| Page | Header | Covers |
|---|---|---|
| [GPIO](gpio.md) | `w80x_gpio.h` | Pin mode, alternate functions, read/write, pin interrupts |
| [UART and printf](uart.md) | `w80x_uart.h` | UART0..5, baud and format, polled I/O, `printf` over a UART |
| [DMA](dma.md) | `w80x_dma.h` | Memory copy, peripheral requests, UART DMA, interrupts |
| [Timers](timer.md) | `w80x_timer.h` | TIMER0..5, one-shot and periodic, interrupts, wall-clock delays |
| [SPI](spi.md) | `w80x_spi.h` | SPI master, modes 0–3, up to 20 MHz |
| [SDIO host](sdh.md) | `w80x_sdh.h` | SD bus: clock, commands, responses, data blocks (CPU or DMA) |
| [Crypto](crypto.md) | `w80x_crypto.h`, `w80x_rsa.h` | CRC, SHA1, MD5, AES, DES/3DES, true RNG, RSA |

### Libraries (`examples/lib`)

| Page | Source | Covers |
|---|---|---|
| [SD card and FatFs](sdcard.md) | `examples/lib/sdcard`, `examples/lib/fatfs` | SD cards over SPI or SDIO, FAT/exFAT file system |
