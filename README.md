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
  w80x.h               memory map, CLK/GPIO/UART/DMA/TIMER/SPI/GPSEC/RSA/WDG/VIC/CORET registers, IRQ numbers, CPU intrinsics
  w80x_clock.h/.c      CPU clock dividers, peripheral clock gating, peripheral reset
  w80x_gpio.h          GPIO driver (header-only): mode, AF, read/write, interrupts
  w80x_wdg.h           watchdog (header-only): start/feed/stop, system reset, reset cause
  w80x_uart.h          UART0..5 (header-only): init, baud, polled TX/RX, interrupts
  w80x_stdio.c         newlib hooks: printf/scanf over a UART, malloc heap
  w80x_utils.h         cycle counting (header-only): CPU_TICKS, cpu_cycles_since(), cycles to us/ms
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

## API at a glance

### Clocks (`w80x_clock.h`)

```c
clock_set_cpu_div(CLK_CPU_240MHZ);  // PLL 480 MHz / 2; also 160, 120, 96, 80, 60, 40, 24, 12, 2 MHz
clock_set_cpu_mhz(160);             // any MHz value that divides 480
clock_cpu_hz(); clock_apb_hz(); clock_wlan_hz(); clock_peri_hz();
clock_enable(CLK_GATE_GPIO | CLK_GATE_UART0);
clock_disable(CLK_GATE_I2S);
periph_reset(CLK_RST_UART1);
clock_jtag_enable();                // PA1/PA4 debug port
```

The CPU clock must be an integer multiple of the AHB2/APB clock (manual
§5.3.4). `clock_set_cpu_div()` picks the WLAN divider and the bus ratio for
you. It keeps WLAN at 160 MHz (APB 40 MHz) whenever the CPU divider allows it,
and lowers WLAN otherwise (for example 96 MHz CPU → APB 24 MHz).
`SystemCoreClock` is updated automatically.

### GPIO (`w80x_gpio.h`)

```c
gpio_mode(PB5, GPIO_OUTPUT);
gpio_mode(PA2, GPIO_INPUT_PULLUP);       // also GPIO_INPUT, GPIO_INPUT_PULLDOWN, GPIO_OUTPUT|GPIO_PULLUP
gpio_set(PB5); gpio_clear(PB5); gpio_toggle(PB5); gpio_write(PB5, 1);
int v = gpio_read(PA2);
gpio_port_write(GPIOB, 0x0F, 0x05);      // masked port write
gpio_af(PB19, GPIO_AF1);                 // route to an alternate function
gpio_analog(PA1);

gpio_irq_enable(PA2, GPIO_IRQ_FALLING);  // RISING, FALLING, BOTH, HIGH, LOW
irq_enable(GPIOA_IRQn);
__enable_irq();
W80X_IRQ void GPIOA_IRQHandler(void) { gpio_irq_clear(PA2); /* ... */ }
```

Pins are plain integers (`PA0..PA15` = 0..15, `PB0..PB31` = 32..63), so they
can be stored in tables or passed around.

Notes from the register manual:
- Every pin starts with its alternate function enabled. `gpio_mode()` switches it to GPIO.
- The pull-up register is active-low and the pull-down register is active-high. The driver hides this.
- Some pins have fixed roles: PA0 = BOOTMODE, PA1/PA4 = JTAG, PB19/PB20 = UART0 (used for flashing).

### Watchdog (`w80x_wdg.h`)

```c
if (reset_status() & CLK_RSTSTAT_WDG) { /* the last reset came from the watchdog */ }
reset_status_clear();

wdg_start_ms(2000);       // reset at most 2 s after the last feed
for (;;) { work(); wdg_feed(); }   // feed at least every 1 s (timeout / 2)

wdg_stop();
wdg_system_reset();       // software reboot (does not return)
```

The hardware resets the chip when a timeout finds the previous timeout's
interrupt still uncleared. Feeding clears it and restarts the countdown, so the
interrupt comes one period and the reset two periods after the last feed.
`wdg_start_ms()` sets the period to half the timeout.
The counter runs on the APB clock: call `wdg_start_ms()` again after
`clock_set_cpu_div()`. Enabling `WDG_IRQn` gives an early warning one period
(half the timeout) before the reset. See the header for the rules a handler must follow.

### UART and printf (`w80x_uart.h`)

```c
uart0_init(115200);            // PB19 TX / PB20 RX, the USB-serial port
stdio_set_uart(UART0);         // printf, puts, scanf, getchar use UART0
printf("CPU %lu Hz\n", SystemCoreClock);   // "\n" is sent as "\r\n"

uart_puts(UART0, "raw\r\n");   // no stdio, no newline translation
int c = uart_try_getc(UART0);  // -1 if nothing received

uart_pins(PB6, PB7, GPIO_AF1); // other UARTs: route the pins first
uart_init(UART1, 9600);
uart_set_format(UART1, UART_8E1);
```

`printf` is newlib's full version (with `%f`); it adds about 37 KB to the
image, and the first call takes a 1 KB line buffer from the heap. Output
appears at each `\n`, or call `fflush(stdout)`. Programs that don't use stdio
pay nothing. The hooks in `w80x_stdio.c` are weak, so you can supply your own
`_write()`.

The baud divider comes from the APB clock: call `uart_flush()` before and
`uart_set_baud()` after `clock_set_cpu_div()`. Watch the console with
`make monitor` (115200 baud).

### DMA (`w80x_dma.h`)

```c
dma_init();
dma_memcpy(0, dst, src, 1024);               // channel 0, waits

dma_setup(1, dst, src, len, DMA_SRC_INC | DMA_DST_INC | DMA_WIDTH_32, DMA_REQ_MEM);
dma_irq_enable(1, DMA_IRQ_DONE);             // DMA_Channel1_IRQHandler, clear with dma_irq_clear()
irq_enable(dma_irqn(1));
dma_start(1);

dma_uart_tx(2, UART0, buf, n);               // background UART output
```

Eight channels; channels 0-3 have their own interrupt, 4-7 share one.
Transfers are at most 65535 bytes. Use SRAM buffers: DMA from flash isn't
confirmed. Only one UART at a time can use DMA. Wrap (ring) mode and
descriptor chains are included, following the vendor SDKs, but haven't been tried yet.

### Timers (`w80x_timer.h`)

```c
timer_init();
timer_start(TIMER0, 500, TIMER_MS | TIMER_PERIODIC | TIMER_IRQ);   // every 500 ms
irq_enable(TIMER_IRQn);

W80X_IRQ void TIMER_IRQHandler(void)          // shared by all six timers
{
    uint32_t fired = timer_irq_status();      // bit n = timer n
    timer_irq_clear(fired);
    if (fired & BIT(TIMER0)) { /* ... */ }
}

timer_delay_us(250);                          // waits on TIMER5 (TIMER_DELAY_ID)
timer_delay_ms(1500);
```

Six 32-bit timers on a 1 us tick from the APB clock, counting in us or ms.
`timer_delay_us/ms()` measure wall-clock time, so interrupts during the wait
don't stretch them, and they reach 2^32 us or ms. `delay_us/ms()` (CORET)
remain for short busy waits. TIMER5 is reserved for the delay functions;
compile with `-DTIMER_DELAY_ID=TIMERn` to use another. Call `timer_init()` again
after `clock_set_cpu_div()` (the delay functions do it themselves).

### SPI (`w80x_spi.h`)

```c
spi_pins(PB2, PB3, PA7, GPIO_AF2);            // SCK, MISO, MOSI (free pins on the HLK kit)
gpio_set(PB4); gpio_mode(PB4, GPIO_OUTPUT);   // chip select as a plain GPIO
spi_init(8000000, SPI_MODE0);                 // returns the SCK actually set (<= request)

gpio_clear(PB4);
spi_write(cmd, 4);
spi_read(buf, 256);                           // sends 0xFF while reading
gpio_set(PB4);
uint8_t r = spi_xfer(0x9F);                   // one byte each way; spi_transfer(tx, rx, n) for buffers
```

Master only, polled. SCK = APB / (2 * (div + 1)), at most 20 MHz. Data moves
in chunks of up to 32 bytes (the FIFO size), so SCK pauses briefly between
chunks; a GPIO chip select stays low through the whole transfer. The SPI's
own CS pin can be routed with `gpio_af()` and driven with `spi_cs(1/0)`.
Pin options are listed at the top of `w80x_spi.h`.

### Crypto (`w80x_crypto.h`, `w80x_rsa.h`)

```c
crypto_init();

uint32_t c = crc32(buf, len);                 // zlib-compatible; also crc16_modbus/ccitt, crc8, crc_calc()

uint8_t digest[SHA1_SIZE];
sha1(data, len, digest);                      // or sha1_init/update/final; md5() likewise

cipher_ctx_t aes;
aes_setup(&aes, key, CIPHER_CBC, iv);         // CIPHER_ECB / CBC / CTR; des_setup, des3_setup
cipher_encrypt(&aes, plain, out, len);        // len = multiple of the block size
cipher_decrypt(&aes, out, plain, len);

rng_init();                                   // rng_init_analog(): raw, unwhitened bits
uint32_t r = rng_read();                      // true random; rng_fill(buf, n)

rsa_init();
rsa_modexp(out, m, &e, 1, n, 32);             // out = m^e mod n, 1024-bit, little-endian words
```

One engine (GPSEC) does CRC, SHA1, MD5, AES-128, DES, 3DES, RC4 and the
TRNG. It reads and writes the buffers itself and can only read SRAM (from
flash it reads zeros), so const input in flash is copied through RAM. Calls
wait for it to finish. CBC/CTR state lives in the context, so a message can be
encrypted in several calls. AES keys are 128-bit only (hardware limit).
AES-CTR runs in software over hardware ECB, because the hardware CTR mode
builds its counter block in a scrambled order. The TRNG register is a shift
register that takes in one new bit per analog random clock (measured: about
every 6 us), so `rng_read()` waits
until it has changed 32 times (~0.2 ms per word, about 19 KB/s).
The RSA block is a Montgomery multiplier. `rsa_modexp()` builds RSA on top
of it without a bignum library, for moduli of 128..2048 bits. At 240 MHz,
RSA-1024 takes about 19 ms with e = 65537 and 60 ms with the private key;
RSA-2048 about 72 ms and 390 ms. It is not
constant-time, so think before using it with private keys.

### SD card and FatFs (`examples/lib`)

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
13 KB of RAM. `get_fattime()` is weak and returns a fixed date; define your own
when you have a clock.

### System (`w80x.h`)

`delay_us()`, `delay_ms()` and `delay_ticks()` busy-wait on the core timer
(CORET), so they stay correct after a clock change. `irq_enable/disable/
set_priority()`, `irq_save()/irq_restore()`, `__enable_irq()`, `__wait()`.
Handlers are weak symbols: define e.g. `W80X_IRQ void TIMER_IRQHandler(void)`.
They are plain C functions (`W80X_IRQ` expands to nothing and only marks them).
A common entry in `startup.S` saves every register a C function may clobber,
FPU included, and calls the handler. GCC 13's own `__attribute__((isr))` is
not used: it leaves r18-r31 and the FPU registers unsaved, so a handler that
calls a function silently corrupts the code it interrupted.
Define `void w80x_early_init(void)` to run code right after reset, before
startup sets up `.data`/`.bss` and calls `SystemInit()`. Only `const` data and
variables marked `W80X_NOINIT` can be used there. `.noinit` variables (1 KB at the top of RAM, below the stack) are never
loaded or cleared by startup, and keep their value across a watchdog reset.
Power-on and the RESET button lose them, so guard them with a magic value
(see `examples/confirm_reset_values`).
Define `void w80x_fault_hook(uint32_t vec, uint32_t epc, uint32_t epsr)` to
report CPU exceptions (by default they just hang in `Default_Handler`).

## Status

| Part | State |
|---|---|
| Registers: CLK, GPIO, VIC, CORET, WDG, UART, DMA, TIMER, SPI, GPSEC, RSA | manual v2.1 ch. 4/5/9/22, checked against wm_iot_sdk register structs. The WDG lock register (0x40) is only in wm_sdk_w80x |
| UART / printf | tested on hardware (Xuantie-800 GCC 13, newlib) |
| DMA | registers checked against both vendor SDKs; `examples/dma` passes on hardware |
| Timers | manual ch. 19 and both vendor SDKs; `examples/timer` works on hardware |
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
