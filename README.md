# w80x-sdk

A minimal, register-level SDK for the **WinnerMicro W80x** family (W800,
W801, W806), in the spirit of [ch32fun](https://github.com/cnlohr/ch32fun): a few headers,
one startup file, one linker script, one Makefile fragment. No HAL, no RTOS,
no vendor blobs needed to build.

> **Core note:** the W80x family uses the T-Head **XT804** core, which runs the
> **C-SKY CK804 (ABIv2)** instruction set, so it builds with a C-SKY GCC
> toolchain (see below).

> **Chips:** the W800, W801 and W806 share the core and the peripheral
> register map, so one SDK covers them. It has only been tested on a W801
> (the HLK-W801-KIT). Where the chips differ (package pins, flash size, the
> W806 having no radio), chip-specific defines such as `W801` will be added as
> needed. Until then, check the pins and the linker script's flash end
> (2 MB) for your part.

Register-level documentation, written as an English datasheet, is in
[`docs/`](docs/README.md).

## Layout

```
w80x/                  the library
  w80x.h               memory map, CLK/PMU/RTC/GPIO/UART/DMA/TIMER/SPI/GPSEC/RSA/WDG/VIC/CORET registers, IRQ numbers, CPU intrinsics
  w80x_clock.h/.c      CPU clock dividers, peripheral clock gating, peripheral reset
  w80x_gpio.h          GPIO driver (header-only): mode, AF, read/write, interrupts
  w80x_wdg.h           watchdog (header-only): start/feed/stop, system reset, reset cause
  w80x_rtc.h           RTC (header-only): struct tm / time_t, time(), alarm, 32 kHz clock source
  w80x_uart.h          UART0..5 (header-only): init, baud, polled TX/RX, interrupts
  w80x_stdio.c         newlib hooks: printf/scanf over a UART, malloc heap, time() from the RTC
  w80x_utils.h         cycle counting (header-only): CPU_TICKS, cpu_cycles_since(), cpu_cycles_step(), cycles to/from us/ms
  w80x_dma.h           DMA (header-only): memory copy, peripheral requests, UART TX/RX, interrupts
  w80x_timer.h         TIMER0..TIMER5 (header-only): one-shot/periodic, interrupts, timer_delay_us/ms
  w80x_spi.h           SPI master (header-only): modes 0-3, up to 20 MHz, polled transfers
  w80x_crypto.h        crypto engine (header-only): CRC, SHA1, MD5, AES-128, DES, 3DES, RC4, TRNG
  w80x_rsa.h           RSA coprocessor (header-only): modular exponentiation up to 2048 bits
  boards/
    hlk_w801_kit.h     HLK-W801-KIT-V1.1: LED pins, active-low led_on()/led_off()
  system.c             C runtime start, SystemInit, delay_us/ms, default fault handler
  startup.S            vector table + Reset_Handler
  w80x.ld              linker script (preprocessed; image address set from the Makefile)
  w80x.mk              shared build rules
tools/
  w80x_img.py          .bin -> .img packer (+ .fls builder, header dump)
  w80x_flash.py        UART flasher (XMODEM-1K to the on-chip bootloader)
examples/              written for the HLK-W801-KIT-V1.1 (see "Board" below)
  blink/               LED blink at 240 MHz
  button_irq/          GPIO falling-edge interrupt toggles an LED (needs an external button)
  clock_switch/        steps the CPU through 240/160/80/40/12 MHz, shown on the LED bar
  watchdog/            feeds the watchdog, then hangs on purpose and gets reset
  uart_printf/         printf on UART0, reads keys, changes the CPU clock on the fly
  dma/                 DMA memory copy (timed against memcpy) and UART output by DMA
  timer/               1 ms and 250 ms timer interrupts, timer_delay_ms() checked against them
  crypto/              self-test of every crypto algorithm against standard test vectors, with speeds
  spi/                 SPI loopback self-test (one wire PA7-PB3): lengths, modes, clocks, throughput
  rtc/                 RTC self-test (rollovers, leap years, alarm, clock accuracy), then a clock on the console
  confirm_reset_values/ reads registers before any init and compares them with the manual's reset values
  sd_fatfs/            SD card over SPI with FatFs: card info, file write/read/verify, speeds, long names
  lib/                 code shared by examples (not built on its own)
    fatfs/             FatFs R0.16 (ChaN) + diskio_sdcard.c glue; fatfs.mk
    sdcard/            SD/MMC block API (sdcard.h), shared CRC/CSD/CID code, SPI backend; sdcard_spi.mk
```

## Board

The examples target the **HLK-W801-KIT-V1.1** from Hi-Link
([docs](https://h.hlktech.com/Mobile/download/fdetail/170.html)).
`w80x/boards/hlk_w801_kit.h` describes it:

| | Pin |
|---|---|
| LED1..LED7 (schematic numbering) | PB25, PB26, PB18, PB16, PB17, PB11, PB5 |
| USB serial (flashing, console) | UART0: PB19 TX, PB20 RX |
| BOOT / RESET buttons | special pins (PA0 BOOTMODE, chip reset), not usable as inputs |

- **The LEDs are active low.** Their anodes go to 3.3 V, so a pin driven *low*
  turns its LED on: `gpio_clear(LED1)` = on, `gpio_set(LED1)` = off. The board
  header wraps this as `led_on()`, `led_off()`, `led_toggle()`,
  `led_write(pin, on)` and `led_init()` (output, starting off).
- **There is no user button.** `button_irq` needs a push button wired between
  `BUTTON_PIN` (default `PB6`) and GND.

For another board, pass different pins (`make CFLAGS=-DLED_PIN=PB0`) or add a
header in `w80x/boards/`.

## Toolchain

Any C-SKY bare-metal GCC works. `w80x.mk` looks for these prefixes in `PATH`:
`csky-unknown-elf-`, `csky-elfabiv2-`, `csky-elf-`, `csky-abiv2-elf-`.
You can also pass one directly: `make PREFIX=/opt/csky/bin/csky-elfabiv2-`.

> "Xuantie-800" in a toolchain name means T-Head's 800-series cores, which
> include the XT804/E804 in the W80x; they all use the C-SKY v2 instruction set.

There are three ways to get a toolchain. Option A is the quickest.

### Option A: prebuilt Xuantie-800 GCC from a GitHub mirror

The [crosstyan/Xuantie-800-gcc-elf-newlib-prebuilt](https://github.com/crosstyan/Xuantie-800-gcc-elf-newlib-prebuilt/releases)
repository rebuilds T-Head's Xuantie-800 GCC with newlib. It's community-maintained, not official.
The prefix is `csky-unknown-elf-`.

| Host | Release asset |
|---|---|
| Linux x86_64 | `v0.1.1/Xuantie-800-gcc-elf-newlib-x86_64.tar.xz` |
| Windows (MinGW) | `v0.1.1/Xuantie-800-gcc-elf-newlib-mingw.tar.xz` |
| Linux aarch64 | `v0.1/Xuantie-800-gcc-elf-newlib-aarch64-unknown-linux-gnu.tar.xz` |
| macOS Apple Silicon | `v0.1/Xuantie-800-gcc-elf-newlib-aarch64-apple-darwin.tar.xz` |

Linux x86_64 example (download about 530 MB):

```sh
mkdir -p ~/opt && cd ~/opt
curl -LO https://github.com/crosstyan/Xuantie-800-gcc-elf-newlib-prebuilt/releases/download/v0.1.1/Xuantie-800-gcc-elf-newlib-x86_64.tar.xz
tar xf Xuantie-800-gcc-elf-newlib-x86_64.tar.xz
rm Xuantie-800-gcc-elf-newlib-x86_64.tar.xz

# add to your shell profile (~/.bashrc, ~/.zshrc, ...)
export PATH="$HOME/opt/Xuantie-800-gcc-elf-newlib-x86_64/bin:$PATH"

# check it
csky-unknown-elf-gcc --version
csky-unknown-elf-gcc -mcpu=ck804ef -mhard-float -dM -E - </dev/null | grep -i __csky__
```

For other hosts, replace the file name in the URL with one from the table.
On macOS, if Gatekeeper blocks the binaries, run
`xattr -dr com.apple.quarantine ~/opt/Xuantie-800-gcc-elf-newlib-*`.

### Option B: the vendor toolchain (`csky-elfabiv2-`)

Both vendor SDKs use T-Head's older `csky-elfabiv2` GCC (Linux x86_64 build
`csky-elfabiv2-tools-x86_64-minilibc-20210423.tar.gz`). You can get it from
these mirrors:

- WinnerMicro: <https://doc.winnermicro.net/download/tools/linux/csky-elfabiv2-tools-x86_64-minilibc-20210423.tar.gz>
  (linked from the [wm_iot_sdk Linux guide](https://github.com/winnermicro/wm_iot_sdk/blob/master/docs/zh_CN/get_started/linux.rst))
- wm_sdk_w80x releases: <https://github.com/cjacker/wm_sdk_w80x/releases/download/init/csky-elfabiv2-tools-x86_64-minilibc-20210423.tar.gz>

Newer builds of the same toolchain (still GCC 6.3, prefix `csky-elfabiv2-`)
are collected in
[Taixin-Semiconductor/XuanTie-CSKY-Toolchains](https://github.com/Taixin-Semiconductor/XuanTie-CSKY-Toolchains):
the `20250328` vendor release for Linux x86_64 and Windows (MinGW), plus
macOS arm64 packages (a native build, and one that runs the Linux binaries).
The release assets come with SHA-256 sums. Extract them the same way as below.

The tarball has no top-level directory, so extract it into its own folder:

```sh
mkdir -p ~/opt/csky-elfabiv2 && tar xf csky-elfabiv2-tools-x86_64-minilibc-20210423.tar.gz -C ~/opt/csky-elfabiv2
export PATH="$HOME/opt/csky-elfabiv2/bin:$PATH"
```

Both compilers work. Interrupt handlers do not rely on the compiler's `isr`
attribute (see System below), so no compiler-specific flags are needed.

### Option B2: official T-Head download

1. Open the [T-Head OCC download page](https://occ.t-head.cn/community/download).
   You need a free account, and the site is mostly in Chinese.
2. Go to **工具 (Tools)**, then **工具链-800系列 (Toolchain, 800 series)**.
3. Download either of these for your OS:
   - `Xuantie-800-gcc-elf-newlib-<host>-<version>.tar.gz`, the current release (prefix `csky-unknown-elf-`), or
   - `csky-elfabiv2-tools-<host>-minilibc-<date>.tar.gz`, the older release used by the vendor W80x SDKs (prefix `csky-elfabiv2-`).
4. Extract it and add its `bin/` directory to `PATH` as in option A.

### Option C: build from upstream GNU sources

Upstream GCC (since 9) and binutils (since 2.32) support the `csky-elf`
target and `-mcpu=ck804ef`. That means you can avoid vendor binaries entirely.
With [crosstool-ng](https://crosstool-ng.github.io/), select target
architecture `csky` with the bare-metal `elf` OS and newlib. You can also
build binutils, gcc and newlib by hand with `--target=csky-elf`. The result
uses the `csky-elf-` prefix, which `w80x.mk` detects automatically.

### Using it

`w80x.mk` looks for these prefixes in `PATH`, in this order:
`csky-unknown-elf-`, `csky-elfabiv2-`, `csky-elf-`, `csky-abiv2-elf-`.
If the toolchain isn't on `PATH`, pass the prefix directly:

```sh
make PREFIX=$HOME/opt/Xuantie-800-gcc-elf-newlib-x86_64/bin/csky-unknown-elf-
```

Flashing needs Python 3 and pyserial: `pip install pyserial`.

### VS Code

`.vscode/c_cpp_properties.json` sets up the Microsoft C/C++ extension
(`ms-vscode.cpptools`) so Go to Definition (F12), hover and completion work
across the SDK and examples. If your toolchain isn't in
`/opt/Xuantie-800-gcc-elf-newlib-x86_64`, change `compilerPath` there; that
path is also how the extension finds the newlib headers (`stdio.h`, ...).

## Build & flash

```sh
cd examples/blink
make                              # -> build/blink.elf, .bin, .img
make flash PORT=/dev/ttyUSB0      # auto-reset via RTS, 2 Mbaud download
make flash BAUD=115200            # if the high-speed download is unreliable
make monitor                      # serial console @115200 on UART0 (typed keys are sent; Ctrl-C quits)
```

`make` at the top level builds every example.

A new project only needs a Makefile like this:

```make
TARGET := myapp
include path/to/w80x-sdk/w80x/w80x.mk
```

All `*.c` / `*.S` files in the project directory are compiled. Set `SRCS` to
override that.

## Boot flow and image format

```
0x08000000  ROM params / reserved
0x08002000  secboot header         (vendor secure bootloader, factory-installed)
0x08002400  secboot code
0x08010000  OTA / update area
0x080D0000  application header     <- IMG_HEADER_ADDR
0x080D0400  application code       <- IMG_RUN_ADDR (vector table first)
0x081F0000  system parameters
```

On power-up the mask ROM starts **secboot**. Secboot checks the 64-byte
application header (magic `0xA0FFFF9F`, CRC-32 of header and payload) and
starts the app from its vector table. `tools/w80x_img.py` builds that header.
Its output is byte-identical to the vendor `wm_tool` (checked against
`w800_secboot.img`).

- **`.img`** is the app only. Flash it with the stock secboot that ships on
  every module. This is the default (`make flash`).
- **`.fls`** is secboot plus the app. You only need it if secboot was erased:
  the chip then answers `Mac:` from ROM, and `w80x_flash.py` tells you so. The
  secboot binary is not part of this repo. Take `tools/w800/w800_secboot.img`
  from [wm_sdk_w80x](https://github.com/cjacker/wm_sdk_w80x) and run
  `make fls SECBOOT=/path/w800_secboot.img && make flash-fls`.
- The factory secboot (also used by
  [wm_sdk_w80x](https://github.com/cjacker/wm_sdk_w80x)) expects the app header
  at `0x080D0000`. That is the default (`BOOT=secboot`).
- Firmware built with the official
  [wm_iot_sdk](https://github.com/winnermicro/wm_iot_sdk) replaces secboot with
  its own bootloader and a partition table. Its app partition starts at
  `0x0800F000`. If your chip ran such firmware, build with `make BOOT=iot`,
  otherwise the image is written to flash but never started. The header format
  and the UART download protocol are the same for both.
- For any other layout, set `IMG_HEADER_ADDR` (and `IMG_UPD_ADDR`) directly.
  The linker script and image header follow this setting.
- If auto-reset doesn't work, keep **BOOTMODE (PA0)** low for more than 30 ms
  during reset. That forces the ROM download mode, and `.fls` images can then
  be flashed.

`python3 tools/w80x_img.py info build/blink.img` dumps and verifies headers.

### Building wm_iot_sdk examples

To build the official [wm_iot_sdk](https://github.com/winnermicro/wm_iot_sdk)
examples next to this SDK (for example to compare its bootloader with
secboot), these changes were needed. They were tested with wm_iot_sdk commit
`e0e4a72` (2024-12-04), CMake 4.2 and the `csky-elfabiv2` 20210423 toolchain:

1. **Toolchain:** wm_iot_sdk needs the vendor `csky-elfabiv2-` GCC
   ([Option B](#option-b-the-vendor-toolchain-csky-elfabiv2-)). Xuantie-800
   GCC 13 has a different prefix.
2. **Python venv:** create one with the SDK's requirements:
   ```sh
   python3 -m venv $WM_IOT_SDK_PATH/venv
   $WM_IOT_SDK_PATH/venv/bin/pip install -r $WM_IOT_SDK_PATH/tools/wm/requirements.txt
   ```
3. **PATH:** `wm.py` runs its helper scripts (`wmdt.py` and others) as
   `python3` from `PATH`. So the venv and the toolchain must both be on `PATH`,
   or configuring fails with `No module named 'tomlkit'`:
   ```sh
   export WM_IOT_SDK_PATH=/path/to/wm_iot_sdk
   export PATH=$WM_IOT_SDK_PATH/venv/bin:/path/to/csky-elfabiv2/bin:$PATH
   cd $WM_IOT_SDK_PATH/examples/hello_world && python3 $WM_IOT_SDK_PATH/tools/wm/wm.py build
   ```
4. **CMake 4.x link fix:** the SDK replaces CMake's link rule with one that
   has no `<LINK_FLAGS>` placeholder. CMake 4.x passes the SDK's link flags
   through that placeholder, so the link step gets no `-mcpu=ck804ef
   -mhard-float -nostartfiles`. gcc then links for its default CPU (ck810),
   and the build fails with `arch flag ck803 conflict with target ck810`.
   Add `<LINK_FLAGS>` to both rules in `tools/cmake/compile.cmake`:
   ```diff
   -    set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <CMAKE_C_LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
   -    set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <CMAKE_CXX_LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
   +    set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
   +    set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
   ```
   Older CMake versions leave `<LINK_FLAGS>` empty here, so the fix is safe
   with them too. Linking `libgcc.a` by hand or using a custom specs file is
   not needed: with the right `-mcpu`, gcc finds the right libgcc.
5. **Flashing changes the boot layout:** the wm_iot_sdk `.fls` replaces
   secboot with the wm_iot_sdk bootloader at `0x08002000`, a partition table at
   `0x0800E000` and the app at `0x0800F000`. After that, flash this SDK's
   examples with `make BOOT=iot flash` (the `.img` only: `SECBOOT` and
   `make fls` are for the classic secboot, and `make BOOT=iot fls` is
   refused). Changing `BOOT` rebuilds the image; `confirm_reset_values`
   prints which layout it was linked for. To go back, flash the secboot `.fls`
   (`make fls SECBOOT=... && make flash-fls`).
6. **Hardware notes depend on the bootloader:** everything in
   [docs/](docs/README.md) tagged *(hardware)* was read with the cjacker
   secboot. Run `examples/confirm_reset_values` (built with `BOOT=iot`) to see
   what the wm_iot_sdk bootloader leaves in the registers and RAM.

## API

The API is described in [`docs/api/`](docs/api/README.md), one page per
header, each with a short usage example and the points to watch.

## Status

| Part | State |
|---|---|
| Registers: CLK, GPIO, VIC, CORET, WDG, UART, DMA, TIMER, SPI, GPSEC, RSA | manual v2.1 ch. 4/5/9/22, checked against wm_iot_sdk register structs. The WDG lock register (0x40) is only in wm_sdk_w80x |
| UART / printf | tested on hardware (Xuantie-800 GCC 13, newlib) |
| DMA | registers checked against both vendor SDKs; `examples/dma` passes on hardware |
| Timers | manual ch. 19 and both vendor SDKs; `examples/timer` works on hardware |
| RTC | manual ch. 20/21 and both vendor SDKs (which correct the manual); builds; `examples/rtc` not yet run on hardware |
| SPI master | manual ch. 14 and wm_iot_sdk HAL; builds; `examples/spi` (loopback) not yet run on hardware |
| SD card (SPI) + FatFs | `examples/sd_fatfs` passes on hardware with two SDHC cards (32 GB and 16 GB, FAT32) at 20 MHz: raw read 820-1180 KB/s; through FatFs ~350-430 KB/s write, ~530 KB/s read |
| Crypto / RSA | tested on hardware: `examples/crypto` passes every algorithm against standard vectors, including RSA-1024 and RSA-2048 |
| Image packer | verified byte-exact against vendor output |
| Flasher | used for every hardware test so far |
| Startup, linker, interrupts | tested on hardware; interrupts use the common entry in `startup.S` |
| Examples | all run on the HLK-W801-KIT-V1.1 |

Planned next: PWM, I2C, PMU/sleep; SPI by DMA and slave mode.

## References

- [FatFs](https://elm-chan.org/fsw/ff/) R0.16 by ChaN (BSD-style license, `examples/lib/fatfs/LICENSE.txt`),
  and the SD Association's Physical Layer Simplified Specification (SPI mode, ch. 7).
- WM_W800 Register Manual v2.1 (WinnerMicro). It covers the W801 and W806 too.
- [CK804 User Manual](https://www.xrvm.cn/community/download?id=4222813196513251328)
  (T-Head, free login). The XT804 is a CK804 core; this manual documents the
  core peripherals the W800 register manual leaves out: the VIC (`VIC_TypeDef`), the
  CORET timer, control registers and low-power modes.
- [C-SKY Architecture User Guide](https://github.com/c-sky/csky-doc/blob/master/CSKY%20Architecture%20user_guide.pdf):
  the C-SKY ISA, used for `startup.S` and the inline assembly in `w80x.h`
  (`psrset`, `mtcr`/`mfcr`, `wait`, interrupt entry and exit).
- [cjacker/wm_sdk_w80x](https://github.com/cjacker/wm_sdk_w80x): the classic
  W800/W801 vendor SDK with a Linux build. Used for `wm_tool.c` (image format,
  download protocol), `startup.S`, `gcc_csky.ld`, `wm_regs.h` and `w800_secboot.img`.
- [winnermicro/wm_iot_sdk](https://github.com/winnermicro/wm_iot_sdk): the
  official FreeRTOS-based SDK. Its register structs (`components/wm_soc/w80x/include/*_reg_struct.h`)
  and low-level drivers (`components/wm_hal/src/w80x/wm_ll/`) were used to check
  our CLK, GPIO and WDG definitions. `tools/wm/bin2img.py` and `flash.py`
  confirm the image header and the download protocol, and the partition tables
  give the `BOOT=iot` layout.
