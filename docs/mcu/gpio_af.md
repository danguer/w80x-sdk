# Pin alternate functions (GPIO AF)

Every W80x pin is either a GPIO or one of up to four peripheral signals
(alternate functions AF1..AF4), and some pins also have an analog
function (ADC or touch). This chapter lists them for every pin of the
W801/W806 (QFN56: PA0..PA15, PB0..PB27), and marks the pins that the
W800 (QFN32) also has.

- **Registers:** GPIOA at `0x4001_1200`, GPIOB at `0x4001_1400` (APB);
  the three that pick the function are in
  [Selecting a function](#selecting-a-function).
- **SDK:** `gpio_af()`, `gpio_mode()` and `gpio_analog()` in
  [`w80x_gpio.h`](../../w80x/w80x_gpio.h) ([API](../api/gpio.md)). The
  peripheral drivers take the AF as an argument (`spi_pins(..., GPIO_AF2)`,
  `pwm_pin(PB25, GPIO_AF2)`).
- **Source:** wm_sdk_w80x `wm_gpio_afsel.c` (one function per signal, the
  only complete list), checked against the manual's pin table (chapter 30,
  W800 pins only) and wm_iot_sdk's device tables
  (`components/wm_dt/config/w806/wm_dt_hw.c`). All three agree on the pin
  and AF numbers except where
  [Manual discrepancies](#manual-discrepancies) says otherwise.

> **Status:** seen on hardware (HLK-W801-KIT): UART0 on PB19/PB20 (AF1),
> SPI on PB2/PB3/PA7 (AF2), PWM3 on PB25 and PWM4 on PB26 (AF2). The rest
> is *(SDK)*.

**Contents:** [Selecting a function](#selecting-a-function) ·
[By pin](#by-pin) · [By peripheral](#by-peripheral) ·
[Analog functions](#analog-functions) · [Reserved pins](#reserved-pins) ·
[Manual discrepancies](#manual-discrepancies)

---

## Selecting a function

Three registers per port pick the function, one bit per pin
(bit n = pin n):

| Register | GPIOA | GPIOB | SDK | Meaning |
|---|---|---|---|---|
| GPIO_AF_SEL | `0x4001_1210` | `0x4001_1410` | `port->AF_SEL` | 1: alternate function · 0: GPIO |
| GPIO_AF_S1 | `0x4001_1214` | `0x4001_1414` | `port->AF_S1` | AF number, high bit |
| GPIO_AF_S0 | `0x4001_1218` | `0x4001_1418` | `port->AF_S0` | AF number, low bit |

| AF_SEL | AF_S1 | AF_S0 | Function | SDK |
|---|---|---|---|---|
| 1 | 0 | 0 | AF1 | `GPIO_AF1` |
| 1 | 0 | 1 | AF2 | `GPIO_AF2` |
| 1 | 1 | 0 | AF3 | `GPIO_AF3` |
| 1 | 1 | 1 | AF4 | `GPIO_AF4` |
| 0 | — | — | GPIO (DIR, PULL_EN, DN_EN apply) | `gpio_mode()` |
| 0 | — | — | Analog: GPIO input, no pull-up or pull-down | `gpio_analog()` |

- **After reset** AF_SEL is all ones and AF_S1/AF_S0 are 0, so every pin
  starts on AF1 *(manual)*. The vendor startup (`wm_gpio_af_disable()`)
  then sets every pin to GPIO except PA1/PA4, which stay on AF1 (SWD)
  *(SDK)*.
- **Analog** (ADC and touch) has no AF code: the pin is set to a GPIO input
  with both pulls off, and the ADC or touch controller then uses it. The
  vendor SDKs number their pin options 1..7: 1..4 are AF1..AF4, 5 is GPIO,
  6 is ADC (and segment LCD), 7 is touch *(SDK)*.
- **Peripheral outputs can't be read back:** while a pin is on an AF that
  drives it (SPI SCK, PWM, UART TX), its GPIO_DATA bit reads 0, even when
  the pin is high. AF inputs (SPI MISO) do read the pin *(hardware)*.

The register bits are described in the manual's GPIO chapter (9.4.6 to
9.4.8).

## By pin

Signal names are this SDK's (see the [notes](#signal-names) below the
table). PWM channels are numbered from 0. The **W800** column marks the
pins the 32-pin W800 also has.

| Pin | AF1 | AF2 | AF3 | AF4 | Analog | W800 | Notes, HLK kit |
|---|---|---|---|---|---|---|---|
| PA0 | I2S_MCLK | SPI_CS | PWM2 | I2S_DO | — | ✓ | BOOTMODE strap |
| PA1 | SWCLK | I2C_SCL | PWM3 | I2S_LRCK | ADC0 | ✓ | SWD |
| PA2 | UART1_RTS | UART2_TX / 7816_IO | PWM0 | UART3_RTS | ADC3 |  |  |
| PA3 | UART1_CTS | UART2_RX | PWM1 | UART3_CTS | ADC2 |  |  |
| PA4 | SWDIO | I2C_SDA | PWM4 | I2S_BCK | ADC1 | ✓ | SWD |
| PA5 | UART3_TX | UART2_RTS / 7816_CLK | PWM_BRAKE | UART4_RTS | — |  |  |
| PA6 | UART3_RX | UART2_CTS | — | UART4_CTS | — |  |  |
| PA7 | PWM4 | SPI_MOSI | I2S_MCLK / EXTCLK | I2S_DI | TOUCH1 | ✓ |  |
| PA8 | PWM_BRAKE | UART4_TX | UART5_TX | I2S_BCK | — |  |  |
| PA9 | SDH_CLK | UART4_RX | UART5_RX | I2S_LRCK | TOUCH2 |  |  |
| PA10 | SDH_CMD | UART4_RTS | PWM0 | I2S_DO | TOUCH3 |  |  |
| PA11 | SDH_DAT0 | UART4_CTS | PWM1 | I2S_DI | — |  |  |
| PA12 | SDH_DAT1 | UART5_TX | PWM2 | — | TOUCH14 (CMOD) |  |  |
| PA13 | SDH_DAT2 | UART5_RX | PWM3 | — | — |  |  |
| PA14 | SDH_DAT3 | UART5_RTS | PWM4 | — | TOUCH15 (CDC) |  |  |
| PA15 | PSRAM_CK | UART5_CTS | PWM_BRAKE | — | — |  |  |
| PB0 | PWM0 | SPI_MISO | UART3_TX | PSRAM_CK | TOUCH4 | ✓ |  |
| PB1 | PWM1 | SPI_SCK | UART3_RX | PSRAM_CS | TOUCH5 | ✓ |  |
| PB2 | PWM2 | SPI_SCK | UART2_TX / 7816_IO | PSRAM_D0 | TOUCH6 | ✓ |  |
| PB3 | PWM3 | SPI_MISO | UART2_RX | PSRAM_D1 | TOUCH7 | ✓ |  |
| PB4 | SPI_CS | UART2_RTS / 7816_CLK | UART4_TX | PSRAM_D2 | TOUCH8 | ✓ |  |
| PB5 | SPI_MOSI | UART2_CTS | UART4_RX | PSRAM_D3 | TOUCH9 | ✓ | LED7 |
| PB6 | UART1_TX | SDH_CLK | HSPI_SCK | SDIO_CLK | TOUCH10 | ✓ |  |
| PB7 | UART1_RX | SDH_CMD | HSPI_INT | SDIO_CMD | TOUCH11 | ✓ |  |
| PB8 | I2S_BCK | SDH_DAT0 | PWM_BRAKE | SDIO_DAT0 | TOUCH12 | ✓ |  |
| PB9 | I2S_LRCK | SDH_DAT1 | HSPI_CS | SDIO_DAT1 | TOUCH13 | ✓ |  |
| PB10 | I2S_DI | SDH_DAT2 | HSPI_MOSI | SDIO_DAT2 | — | ✓ |  |
| PB11 | I2S_DO | SDH_DAT3 | HSPI_MISO | SDIO_DAT3 | — | ✓ | LED6 |
| PB12 | HSPI_SCK | PWM0 | UART5_RTS | I2S_BCK | — |  |  |
| PB13 | HSPI_INT | PWM1 | UART5_CTS | I2S_LRCK | — |  |  |
| PB14 | HSPI_CS | PWM2 | SPI_CS | I2S_DO | — |  |  |
| PB15 | HSPI_MOSI | PWM3 | SPI_SCK | I2S_DI | — |  |  |
| PB16 | HSPI_MISO | PWM4 | SPI_MISO | UART1_RX | — |  | LED4 |
| PB17 | UART5_RX | PWM_BRAKE | SPI_MOSI | I2S_MCLK / EXTCLK | — |  | LED5 |
| PB18 | UART5_TX | — | — | — | — |  | LED3 |
| PB19 | UART0_TX | PWM0 | UART1_RTS | I2C_SDA | — | ✓ | Console, flashing |
| PB20 | UART0_RX | PWM1 | UART1_CTS | I2C_SCL | — | ✓ | Console, flashing |
| PB21 | — | — | — | — | — |  | GPIO only |
| PB22 | — | — | — | — | — |  | GPIO only |
| PB23 | SPI_CS | — | — | — | — |  |  |
| PB24 | SPI_SCK | PWM2 | — | — | — |  |  |
| PB25 | SPI_MISO | PWM3 | — | — | — |  | LED1 |
| PB26 | SPI_MOSI | PWM4 | — | — | — |  | LED2 |
| PB27 | PSRAM_CS | UART0_TX | — | — | — |  |  |

### Signal names

- **SPI** is the general SPI master/slave (LSPI in the manual).
  **HSPI** is the high-speed SPI slave; the vendor names its data pins DI
  and DO (from the slave's side), which are MOSI and MISO here.
- **SDH** is the SDIO host (SD cards; MMC in the manual), **SDIO** the SDIO
  device (slave) controller.
- **UART2** can also run as the ISO 7816 smart-card interface: TX is then
  the card's I/O line and RTS its clock.
- **I2S_MCLK / EXTCLK:** on PA7 (AF3) and PB17 (AF4) the same AF is the
  master clock output or an external clock input, depending on the I2S
  setup *(SDK)*. PA0 (AF1) is MCLK only.
- **SWCLK / SWDIO** are the debug port; the manual and wm_sdk_w80x call
  them JTAG_CK and JTAG_SWO.

## By peripheral

| Peripheral | Signal | Pins (AF) |
|---|---|---|
| UART | UART0_TX | PB19 (AF1), PB27 (AF2) |
|  | UART0_RX | PB20 (AF1) |
|  | UART1_TX | PB6 (AF1) |
|  | UART1_RX | PB7 (AF1), PB16 (AF4) |
|  | UART1_RTS | PA2 (AF1), PB19 (AF3) |
|  | UART1_CTS | PA3 (AF1), PB20 (AF3) |
|  | UART2_TX / 7816_IO | PA2 (AF2), PB2 (AF3) |
|  | UART2_RX | PA3 (AF2), PB3 (AF3) |
|  | UART2_RTS / 7816_CLK | PA5 (AF2), PB4 (AF2) |
|  | UART2_CTS | PA6 (AF2), PB5 (AF2) |
|  | UART3_TX | PA5 (AF1), PB0 (AF3) |
|  | UART3_RX | PA6 (AF1), PB1 (AF3) |
|  | UART3_RTS | PA2 (AF4) |
|  | UART3_CTS | PA3 (AF4) |
|  | UART4_TX | PA8 (AF2), PB4 (AF3) |
|  | UART4_RX | PA9 (AF2), PB5 (AF3) |
|  | UART4_RTS | PA5 (AF4), PA10 (AF2) |
|  | UART4_CTS | PA6 (AF4), PA11 (AF2) |
|  | UART5_TX | PA8 (AF3), PA12 (AF2), PB18 (AF1) |
|  | UART5_RX | PA9 (AF3), PA13 (AF2), PB17 (AF1) |
|  | UART5_RTS | PA14 (AF2), PB12 (AF3) |
|  | UART5_CTS | PA15 (AF2), PB13 (AF3) |
| SPI | SPI_SCK | PB1 (AF2), PB2 (AF2), PB15 (AF3), PB24 (AF1) |
|  | SPI_CS | PA0 (AF2), PB4 (AF1), PB14 (AF3), PB23 (AF1) |
|  | SPI_MISO | PB0 (AF2), PB3 (AF2), PB16 (AF3), PB25 (AF1) |
|  | SPI_MOSI | PA7 (AF2), PB5 (AF1), PB17 (AF3), PB26 (AF1) |
| High-speed SPI slave | HSPI_SCK | PB6 (AF3), PB12 (AF1) |
|  | HSPI_INT | PB7 (AF3), PB13 (AF1) |
|  | HSPI_CS | PB9 (AF3), PB14 (AF1) |
|  | HSPI_MOSI | PB10 (AF3), PB15 (AF1) |
|  | HSPI_MISO | PB11 (AF3), PB16 (AF1) |
| SDIO host | SDH_CLK | PA9 (AF1), PB6 (AF2) |
|  | SDH_CMD | PA10 (AF1), PB7 (AF2) |
|  | SDH_DAT0 | PA11 (AF1), PB8 (AF2) |
|  | SDH_DAT1 | PA12 (AF1), PB9 (AF2) |
|  | SDH_DAT2 | PA13 (AF1), PB10 (AF2) |
|  | SDH_DAT3 | PA14 (AF1), PB11 (AF2) |
| SDIO device | SDIO_CLK | PB6 (AF4) |
|  | SDIO_CMD | PB7 (AF4) |
|  | SDIO_DAT0 | PB8 (AF4) |
|  | SDIO_DAT1 | PB9 (AF4) |
|  | SDIO_DAT2 | PB10 (AF4) |
|  | SDIO_DAT3 | PB11 (AF4) |
| PSRAM | PSRAM_CK | PA15 (AF1), PB0 (AF4) |
|  | PSRAM_CS | PB1 (AF4), PB27 (AF1) |
|  | PSRAM_D0 | PB2 (AF4) |
|  | PSRAM_D1 | PB3 (AF4) |
|  | PSRAM_D2 | PB4 (AF4) |
|  | PSRAM_D3 | PB5 (AF4) |
| I2C | I2C_SCL | PA1 (AF2), PB20 (AF4) |
|  | I2C_SDA | PA4 (AF2), PB19 (AF4) |
| I2S | I2S_BCK | PA4 (AF4), PA8 (AF4), PB8 (AF1), PB12 (AF4) |
|  | I2S_LRCK | PA1 (AF4), PA9 (AF4), PB9 (AF1), PB13 (AF4) |
|  | I2S_DO | PA0 (AF4), PA10 (AF4), PB11 (AF1), PB14 (AF4) |
|  | I2S_DI | PA7 (AF4), PA11 (AF4), PB10 (AF1), PB15 (AF4) |
|  | I2S_MCLK | PA0 (AF1), PA7 (AF3), PB17 (AF4) |
|  | I2S_EXTCLK | PA7 (AF3), PB17 (AF4) |
| PWM | PWM0 | PA2 (AF3), PA10 (AF3), PB0 (AF1), PB12 (AF2), PB19 (AF2) |
|  | PWM1 | PA3 (AF3), PA11 (AF3), PB1 (AF1), PB13 (AF2), PB20 (AF2) |
|  | PWM2 | PA0 (AF3), PA12 (AF3), PB2 (AF1), PB14 (AF2), PB24 (AF2) |
|  | PWM3 | PA1 (AF3), PA13 (AF3), PB3 (AF1), PB15 (AF2), PB25 (AF2) |
|  | PWM4 | PA4 (AF3), PA7 (AF1), PA14 (AF3), PB16 (AF2), PB26 (AF2) |
|  | PWM_BRAKE | PA5 (AF3), PA8 (AF1), PA15 (AF3), PB8 (AF3), PB17 (AF2) |
| Debug | SWCLK | PA1 (AF1) |
|  | SWDIO | PA4 (AF1) |

## Analog functions

| Function | Pins |
|---|---|
| ADC | ADC0 PA1 · ADC1 PA4 · ADC2 PA3 · ADC3 PA2 |
| Touch | TOUCH1 PA7 · TOUCH2 PA9 · TOUCH3 PA10 · TOUCH4..TOUCH9 PB0..PB5 · TOUCH10..TOUCH13 PB6..PB9 · TOUCH14 PA12 · TOUCH15 PA14 |

- ADC channels are numbered from 0, as in both vendor SDKs' code
  (`wm_adc_config(0)` is PA1). The manual and wm_iot_sdk's comments number
  them from 1 (ADC_1 = PA1) *(SDK, manual)*. Two channels can also be read
  as one differential pair *(manual)*.
- Touch numbers are wm_sdk_w80x's. wm_iot_sdk's W806 device table uses
  PA12 as the touch controller's CMOD capacitor and PA14 as its CDC
  capacitor, not as touch inputs, so those two may need external
  capacitors rather than being free touch pads *(SDK, to check)*.
- The vendor SDKs also drive a segment LCD through option 6 on most pins
  (wm_iot_sdk's `gdc0689` example) *(SDK)*. It isn't covered here.

## Reserved pins

| Pin | Role |
|---|---|
| PA0 | BOOTMODE: held low for more than 30 ms at reset, the ROM enters serial download mode *(manual)*. Usable after boot, but nothing may pull it low at reset. The HLK kit's BOOT button is on it |
| PA1, PA4 | SWD debug (AF1, the reset value). Switching them to another function disconnects the debugger |
| PB19, PB20 | UART0, the console and the flashing port of the bootloader |

## Manual discrepancies

| What | Manual | Here |
|---|---|---|
| PWM channel names | PWM1..PWM5 (PWM1 on PB0) | PWM0..PWM4, as in both SDKs and the PWM registers |
| SPI data on PB0 and PB3 (AF2) | "master out, slave in" (MOSI) | MISO, as in both SDKs. PB3 as MISO passes `examples/spi` and the SD card examples *(hardware)* |
| ADC numbering | ADC_1, ADC_2 (from 1) | ADC0.. (from 0), as in the SDKs' code |
| Pin table | W800 (QFN32) pins only | W801/W806 pins from wm_sdk_w80x |
| PB10 | "I2S_D2", "HSPI_D", "SDIO_D" (cut off) | I2S_DI, HSPI_MOSI, SDIO_DAT2 |
