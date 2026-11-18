# PWM controller (PWM)

The PWM controller (manual chapter 23) has five channels, PWM0..PWM4. Each
one has a 16-bit clock divider, an 8-bit period counter and an 8-bit
compare value. Channels 0 and 1, and channels 2 and 3, can run as
complementary or synchronized pairs, with dead time. Channels 0 and 4 can
also capture an input signal.

- **Base address:** `0x4001_1E00` (APB). Its clock is the APB clock, 40 MHz
  by default.
- **SDK:** `PWM_TypeDef` / `PWM` in [`w80x.h`](../../w80x/w80x.h), driver in
  [`w80x_pwm.h`](../../w80x/w80x_pwm.h) ([API](../api/pwm.md)), example
  [`examples/pwm`](../../examples/pwm/main.c).
- **Source:** WM_W800 Register Manual v2.1, chapter 23, checked against
  wm_iot_sdk (`wm_pwm_reg_struct.h`, `wm_ll_pwm.c`, `wm_hal_pwm.c`) and
  wm_sdk_w80x (`wm_pwm.c`, `wm_gpio_afsel.c`). The register layout agrees
  in all three. See [Manual discrepancies](#manual-discrepancies).

> **Status:** partly run on hardware (HLK-W801-KIT): the CH4 one-shot
> count and its interrupt, and reading an AF pin back. Tagged *(hardware)*.

## Registers

| Offset | Address | Name | SDK | Reset value | Description |
|---|---|---|---|---|---|
| `0x00` | `0x4001_1E00` | [PWM_CLKDIV01](#pwm_clkdiv01-pwm_clkdiv23) | `PWM->CLKDIV01` | `0x0000_0000` | Clock dividers, CH0 and CH1 |
| `0x04` | `0x4001_1E04` | [PWM_CLKDIV23](#pwm_clkdiv01-pwm_clkdiv23) | `PWM->CLKDIV23` | `0x0000_0000` | Clock dividers, CH2 and CH3 |
| `0x08` | `0x4001_1E08` | [PWM_CTL](#pwm_ctl) | `PWM->CTL` | `0x0000_0000` | Counter enables, counter types, modes, polarity, CH0 capture |
| `0x0C` | `0x4001_1E0C` | [PWM_PERIOD](#pwm_period-pwm_pnum-pwm_cmpdat) | `PWM->PERIOD` | `0x0000_0000` | Period, CH0..CH3 (one byte each) |
| `0x10` | `0x4001_1E10` | [PWM_PNUM](#pwm_period-pwm_pnum-pwm_cmpdat) | `PWM->PNUM` | `0x0000_0000` | Periods to generate, CH0..CH3 |
| `0x14` | `0x4001_1E14` | [PWM_CMPDAT](#pwm_period-pwm_pnum-pwm_cmpdat) | `PWM->CMPDAT` | `0x0000_0000` | Compare (duty), CH0..CH3 |
| `0x18` | `0x4001_1E18` | [PWM_DTCTL](#pwm_dtctl) | `PWM->DTCTL` | `0x0000_0000` | Dead time for the complementary pairs |
| `0x1C` | `0x4001_1E1C` | [PWM_INTEN](#pwm_inten) | `PWM->INTEN` | `0x0000_0000` | Interrupt enables |
| `0x20` | `0x4001_1E20` | [PWM_INTSTS](#pwm_intsts) | `PWM->INTSTS` | `0x0000_0000` | Interrupt flags |
| `0x24` | `0x4001_1E24` | [PWM_CAPDAT](#pwm_capdat-pwm_cap2dat) | `PWM->CAPDAT` | `0x0000_0000` | CH0 capture values |
| `0x28` | `0x4001_1E28` | [PWM_BRKCTL](#pwm_brkctl) | `PWM->BRKCTL` | `0x0000_0000` | Brake enable and level |
| `0x2C` | `0x4001_1E2C` | [PWM_CH4_REG1](#pwm_ch4_reg1) | `PWM->CH4_REG1` | `0x0000_0000` | CH4 divider, period, period count |
| `0x30` | `0x4001_1E30` | [PWM_CH4_REG2](#pwm_ch4_reg2) | `PWM->CH4_REG2` | `0x0000_0000` | CH4 compare, counter type, mode, polarity |
| `0x34` | `0x4001_1E34` | [PWM_CAP2DAT](#pwm_capdat-pwm_cap2dat) | `PWM->CAP2DAT` | `0x0000_0000` | CH4 capture values |
| `0x38` | `0x4001_1E38` | [PWM_CAP2CTL](#pwm_cap2ctl) | `PWM->CAP2CTL` | `0x0000_0000` | CH4 capture control, flags, output enable |

**Contents:** [How it works](#how-it-works) · [Waveform](#waveform) ·
[Pins](#pins) · [Using it](#using-it) · [Registers](#register-details) ·
[Manual discrepancies](#manual-discrepancies)

**Legend:** **RW** read/write, **RO** read-only, **W1C** write 1 to clear.
Notes are tagged by source: *(manual)*, *(SDK)* (vendor SDKs or this SDK's
code), *(hardware)* seen on a W801, *(to check)* not yet seen on a chip.

---

## How it works

- **Clock, reset, interrupt.** Clock gate `CLK_GATE_PWM` (SW_CLKG_EN
  bit 13), reset `CLK_RST_PWM` (SW_RST_CTRL bit 22); see
  [Clock and reset](clock.md). Interrupt `PWM_IRQn` (22), shared by all
  channels *(SDK)*.
- **Channel registers.** Channels 0..3 share registers, one byte or one
  bit each. Channel 4 came later and has its own registers at `0x2C`–`0x38`
  for the same fields *(manual)*.
- **Running.** A channel's counter runs while its PWM_CTL CNTEN bit
  (27 + n) is 1. Channels enabled in the same write start in step.
- **Counter types.** PWM output uses the down-counting edge-aligned type (1)
  or the center-aligned type (2). Type 0 (counting up) is for capture only
  *(manual)*.
- **Auto-reload or one-shot.** With CNTMODE = 1 the channel repeats.
  With 0 it outputs PNUM periods, then stops and sets its PIF flag
  *(manual)*. CNTEN then reads 0, so the channel restarts with a plain
  CNTEN write *(hardware, CH4)*.
- **Writing CNTMODE clears the compare value** *(manual)*: set the mode
  before the duty.
- **Output enable.** CH0 and CH4 pins can also be capture inputs:
  PWM_CTL bit 12 (CH0) and PWM_CAP2CTL bit 2 (CH4) must be 0 for output.
  The other channels are always outputs *(manual, SDK)*.

## Waveform

The clock after the divider is f_div = APB / N, where N is the 16-bit
divider (0 and 1 both mean no division). With P the period register and C
the compare register *(manual)*:

| | Edge-aligned (counts down) | Center-aligned |
|---|---|---|
| Period, in f_div clocks | P + 1 | 2 × (P + 1) |
| Output rate | APB / (N × (P + 1)) | APB / (N × 2 × (P + 1)) |
| High time | C + 1 | 2 × C + 1 |
| Steady high | C ≥ P | C > P |
| C = 0 | high 1, low P | high 1, low 2 × P + 1 |
| Limit | P ≤ 255 | P ≤ 254 ("should not be 255") |

- **No 0 % duty.** The shortest high time is one clock. For a steady low,
  stop the channel or invert a steady high *(manual)*. What a stopped
  channel's pin outputs is *(to check)*. wm_sdk_w80x stops the channel when
  asked for duty 0 *(SDK)*.
- **Range.** At APB = 40 MHz: P = 255, N = 65535 gives 2.38 Hz; P = 255,
  N = 1 gives 156.25 kHz; P = 1, N = 1 gives 20 MHz. The manual's feature
  list says 3 Hz–160 kHz (the range at P = 255) and elsewhere a 20 MHz
  maximum *(manual)*.
- **Polarity.** PINV inverts the channel's output (CH0..3: PWM_CTL bits
  5:2; CH4: PWM_CH4_REG2 bit 0).

## Pins

From wm_sdk_w80x's `wm_gpio_afsel.c`, for the W801/W806 *(SDK)*. All pin
functions are listed in [Pin alternate functions](gpio_af.md).

| Channel | AF1 | AF2 | AF3 |
|---|---|---|---|
| PWM0 | PB0 | PB12, PB19 | PA2, PA10 |
| PWM1 | PB1 | PB13, PB20 | PA3, PA11 |
| PWM2 | PB2 | PB14, PB24 | PA0, PA12 |
| PWM3 | PB3 | PB15, PB25 | PA1, PA13 |
| PWM4 | PA7 | PB16, PB26 | PA4, PA14 |
| Brake input | PA8 | PB17 | PA5, PA15, PB8 |

The manual's W800 pin table agrees for PA0, PA1, PB0..PB3, PB19, PB20 and
PB8, but numbers the channels PWM1..PWM5 (its PWM1 on PB0 is channel 0
here and in both SDKs) *(manual)*. wm_iot_sdk's W806 device table uses PB12..PB16 on AF2
*(SDK)*. On the HLK-W801-KIT, LED1 (PB25) is PWM3 and LED2 (PB26) and
LED4 (PB16) are PWM4.

While a pin is routed to the PWM, its GPIO DATA bit reads 0, even with the
output steadily high: the PWM output can't be measured by reading the pin
back *(hardware, PB25)*. Use a jumper to a second pin set as a GPIO input.

## Using it

```c
pwm_init();
pwm_pin(PB25, GPIO_AF2);
pwm_setup(PWM3, 1000, PWM_EDGE);    // N = 156, P = 255: 1001.6 Hz
pwm_set_duty(PWM3, PWM_DUTY_MAX / 2);
pwm_start(PWM3);
```

`pwm_setup()` writes, in this order: CNTEN off; capture off and output
enabled (CH0, CH4); counter type and CNTMODE; divider and period; PINV;
compare. This is the order wm_iot_sdk's HAL uses, except that the HAL
writes the compare before CNTMODE *(SDK)*.

---

## Register details

### PWM_CLKDIV01, PWM_CLKDIV23

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E00`, `0x4001_1E04` | 32 bits | RW | `0x0000_0000` | `PWM->CLKDIV01`, `PWM->CLKDIV23`, `pwm_set_clock()` |

| Bits | Name | Access | Reset | Values |
|---|---|---|---|---|
| 31:16 | CLKDIV1 / CLKDIV3 | RW | 0 | CH1 / CH3 divider, 0..65535 (0 and 1 = no division) |
| 15:0 | CLKDIV0 / CLKDIV2 | RW | 0 | CH0 / CH2 divider |

### PWM_CTL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E08` (base + `0x08`) | 32 bits | RW | `0x0000_0000` | `PWM->CTL` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:27 | CNTEN | RW | 0 | Counter running, bit 27 + n = channel n (CH0..CH4) | `PWM_CTL_CNTEN(n)` |
| 26 | — | — | 0 | Reserved | |
| 25 | CAPINV | RW | 0 | CH0 capture: invert the input | `PWM_CTL_CAPINV` |
| 24 | CPEN | RW | 0 | CH0 capture on (latch the counter into PWM_CAPDAT on each edge) | `PWM_CTL_CPEN` |
| 23:16 | CNTTYPE3..0 | RW | 0 | Counter type, 2 bits per channel (bits 17:16 = CH0): 0 up (capture), 1 down (edge-aligned PWM), 2 center-aligned | `PWM_CTL_CNTTYPE_Msk(n)` |
| 15:14 | TWOSYNCEN | RW | 0 | Bit 14: CH1 follows CH0 · bit 15: CH3 follows CH2 | `PWM_CTL_TWOSYNC(pair)` |
| 13 | — | — | 0 | Reserved | |
| 12 | POEN | RW | 0 | CH0 pin. 0: output · 1: tri-state (capture) | `PWM_CTL_POEN` |
| 11:8 | CNTMODE | RW | 0 | Bit 8 + n, CH0..CH3. 0: one-shot (PNUM periods) · 1: auto-reload | `PWM_CTL_CNTMODE(n)` |
| 7 | — | — | 0 | Reserved | |
| 6 | ALLSYNCEN | RW | 0 | 1: every channel follows CH0 | `PWM_CTL_ALLSYNC` |
| 5:2 | PINV | RW | 0 | Bit 2 + n, CH0..CH3: invert the output | `PWM_CTL_PINV(n)` |
| 1:0 | OUTMODE | RW | 0 | Bit 0: CH0/CH1 complementary · bit 1: CH2/CH3 complementary | `PWM_CTL_OUTMODE_COMP(pair)` |

### PWM_PERIOD, PWM_PNUM, PWM_CMPDAT

One byte per channel: bits 7:0 CH0, 15:8 CH1, 23:16 CH2, 31:24 CH3. CH4's
fields are in PWM_CH4_REG1/REG2.

| Address | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| `0x4001_1E0C` | PWM_PERIOD | RW | `0x0000_0000` | Period P, 0..255 ([Waveform](#waveform)) | `pwm_set_clock()`, `pwm_get_period()` |
| `0x4001_1E10` | PWM_PNUM | RW | `0x0000_0000` | Periods to generate in one-shot mode, then stop and set PIF | `pwm_set_count()` |
| `0x4001_1E14` | PWM_CMPDAT | RW | `0x0000_0000` | Compare C ([Waveform](#waveform)) | `pwm_set_cmp()`, `pwm_set_duty()` |

### PWM_DTCTL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E18` (base + `0x18`) | 32 bits | RW | `0x0000_0000` | `PWM->DTCTL`, `pwm_complementary()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:22 | — | — | 0 | Reserved | |
| 21 | DTEN23 | RW | 0 | Dead time on for CH2/CH3 (only in complementary mode) | `PWM_DTCTL_EN(1)` |
| 20 | DTEN01 | RW | 0 | Dead time on for CH0/CH1 | `PWM_DTCTL_EN(0)` |
| 19:18 | — | — | 0 | Reserved | |
| 17:16 | DTDIV | RW | 0 | Dead-time clock: PWM clock / 1, 2, 4, 8 (shared by both pairs) | `PWM_DTCTL_DIV_Msk` |
| 15:8 | DTCNT23 | RW | 0 | CH2/CH3 dead time, in dead-time clocks | `PWM_DTCTL_CNT_Msk(1)` |
| 7:0 | DTCNT01 | RW | 0 | CH0/CH1 dead time | `PWM_DTCTL_CNT_Msk(0)` |

The manual calls the base clock 40 MHz; it is the APB clock (both SDKs
compute rates from APB) *(SDK)*.

### PWM_INTEN

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E1C` (base + `0x1C`) | 32 bits | RW | `0x0000_0000` | `PWM->INTEN`, `pwm_irq_enable()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:8 | — | — | 0 | Reserved | |
| 7 | DMA_EN | RW | 0 | CH0 capture DMA request | `PWM_INTEN_DMA` |
| 6 | FLIEN | RW | 0 | CH0 capture, falling-edge interrupt | `PWM_INTEN_FLIEN` |
| 5 | RLIEN | RW | 0 | CH0 capture, rising-edge interrupt | `PWM_INTEN_RLIEN` |
| 4:0 | PIEN | RW | 0 | Bit n: period-count interrupt for channel n (CH0..CH4) | `PWM_INTEN_PIEN(n)` |

### PWM_INTSTS

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E20` (base + `0x20`) | 32 bits | RW | `0x0000_0000` | `PWM->INTSTS`, `pwm_irq_status()`, `pwm_irq_clear()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:10 | — | — | 0 | Reserved | |
| 9 | OVERFL | RW | 0 | CH0 capture: the counter overflowed (cleared with CRLIF/CFLIF) | `PWM_INTSTS_OVERFL` |
| 8 | FLIFOV | RW | 0 | CH0 capture: another falling edge while CFLIF was set | `PWM_INTSTS_FLIFOV` |
| 7 | RLIFOV | RW | 0 | CH0 capture: another rising edge while CRLIF was set | `PWM_INTSTS_RLIFOV` |
| 6 | CFLIF | W1C | 0 | CH0 falling edge captured | `PWM_INTSTS_CFLIF` |
| 5 | CRLIF | W1C | 0 | CH0 rising edge captured | `PWM_INTSTS_CRLIF` |
| 4:0 | PIF | W1C | 0 | Bit n: channel n generated its PNUM periods | `PWM_INTSTS_PIF(n)` |

### PWM_CAPDAT, PWM_CAP2DAT

| Address | Name | Access | Bits | Values |
|---|---|---|---|---|
| `0x4001_1E24` | PWM_CAPDAT | RO | 31:16 | CH0 counter at the last falling edge |
| | | | 15:0 | CH0 counter at the last rising edge |
| `0x4001_1E34` | PWM_CAP2DAT | RO | 31:16 / 15:0 | The same for CH4 |

### PWM_BRKCTL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E28` (base + `0x28`) | 32 bits | RW | `0x0000_0000` | `PWM->BRKCTL`, `pwm_brake()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:16 | — | — | 0 | Reserved | |
| 15:11 | BRKCTL | RW | 0 | Bit 11 + n: brake on for channel n | `PWM_BRKCTL_EN(n)` |
| 10:8 | — | — | 0 | Reserved | |
| 7:3 | BKOD | RW | 0 | Bit 3 + n: level channel n outputs while braking (1 = high) | `PWM_BRKCTL_LEVEL(n)` |
| 2:0 | — | — | 0 | Reserved | |

Braking follows the brake input pin; its active level isn't described
*(to check)*.

### PWM_CH4_REG1

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E2C` (base + `0x2C`) | 32 bits | RW | `0x0000_0000` | `PWM->CH4_REG1` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:16 | CLKDIV4 | RW | 0 | CH4 divider | `PWM_CH4_CLKDIV_Msk` |
| 15:8 | PERIOD4 | RW | 0 | CH4 period | `PWM_CH4_PERIOD_Msk` |
| 7:0 | PNUM4 | RW | 0 | CH4 periods to generate | `PWM_CH4_PNUM_Msk` |

### PWM_CH4_REG2

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E30` (base + `0x30`) | 32 bits | RW | `0x0000_0000` | `PWM->CH4_REG2` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:16 | — | — | 0 | Reserved | |
| 15:8 | CMP4 | RW | 0 | CH4 compare | `PWM_CH4_CMP_Msk` |
| 7:5 | — | — | 0 | Reserved | |
| 4:3 | CNTTYPE4 | RW | 0 | CH4 counter type (as CNTTYPE in PWM_CTL) | `PWM_CH4_CNTTYPE_Msk` |
| 2 | — | — | 0 | Reserved | |
| 1 | CNTMODE4 | RW | 0 | 0: one-shot · 1: auto-reload | `PWM_CH4_CNTMODE` |
| 0 | PINV4 | RW | 0 | Invert CH4's output | `PWM_CH4_PINV` |

### PWM_CAP2CTL

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4001_1E38` (base + `0x38`) | 32 bits | RW | `0x0000_0000` | `PWM->CAP2CTL` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:11 | — | — | 0 | Reserved | |
| 10 | DMA_request2 | RW | 0 | CH4 capture DMA request | `PWM_CAP2_DMA` |
| 9 | FLIEN2 | RW | 0 | CH4 falling-edge interrupt | `PWM_CAP2_FLIEN` |
| 8 | RLIEN2 | RW | 0 | CH4 rising-edge interrupt | `PWM_CAP2_RLIEN` |
| 7 | OVERFL2 | RW | 0 | CH4 counter overflow | `PWM_CAP2_OVERFL` |
| 6 | FLIFOV2 | RW | 0 | CH4 falling-edge overrun | `PWM_CAP2_FLIFOV` |
| 5 | RLIFOV2 | RW | 0 | CH4 rising-edge overrun | `PWM_CAP2_RLIFOV` |
| 4 | CFLIF2 | W1C | 0 | CH4 falling edge captured | `PWM_CAP2_CFLIF` |
| 3 | CRLIF2 | W1C | 0 | CH4 rising edge captured | `PWM_CAP2_CRLIF` |
| 2 | POEN2 | RW | 0 | CH4 pin. 0: output · 1: tri-state (capture) | `PWM_CAP2_POEN` |
| 1 | CPEN2 | RW | 0 | CH4 capture on | `PWM_CAP2_CPEN` |
| 0 | CAPINV2 | RW | 0 | CH4 capture: invert the input | `PWM_CAP2_CAPINV` |

---

## Manual discrepancies

| Where | Manual | Vendor SDKs / notes |
|---|---|---|
| Clock | "The input clock is 40MHz" | It is the APB clock: both SDKs compute rates from APB (40 MHz by default, lower at some CPU clocks) |
| Output range | 3 Hz–160 kHz in the feature list, 20 MHz maximum in the overview | 2.38 Hz–156.25 kHz at period 255; up to APB / 2 with shorter periods |
| CNTMODE note | "controls PW3, PW2, PW1 and PW0" | CH0..CH3 only; CH4's mode is PWM_CH4_REG2 bit 1 |
| PWM_INTSTS PIF note | "controls PW4 … PW0" | Bit n = channel n, CH4 included (wm_sdk_w80x enables CH4 with PWM_INTEN bit 4) |
| Pin table | PWM1..PWM5 | Channels 0..4 in both SDKs (PB0 is channel 0) |
| Register names | CH4 registers listed as "PWM_CH4_reg1" (clock divider) and "PWM_CH4_reg2" (control 1) | wm_sdk_w80x names them HR_PWM_CH4_REG1 (divider, period, PNUM) and HR_PWM_CH4_REG2 (compare, type, mode, polarity), as used here |
| CH4 compare text | "CH4 period register value" for CMP4 | It is the compare value |
| wm_sdk_w80x dead time | — | `tls_pwm_deadzone_config()` writes PWM_DTCTL with PWM_CTL's value when it sets the enable bit (a bug in that SDK); this SDK does a read-modify-write of PWM_DTCTL |
