# Real-time clock (RTC)

The RTC is a calendar counter inside the power management unit (PMU, manual
chapters 20 and 21). It counts years, months, days, hours, minutes and
seconds, handles month lengths and leap years, and has one alarm that raises
the PMU interrupt. The alarm can also wake the chip from sleep.

- **Base address:** PMU `0x4000_0D00`. The two RTC registers are at PMU
  offsets `0x0C` and `0x10`, and the alarm flag is in the PMU interrupt
  register at `0x14`.
- **SDK:** `PMU_TypeDef` / `PMU` in [`w80x.h`](../../w80x/w80x.h), driver in
  [`w80x_rtc.h`](../../w80x/w80x_rtc.h), example in
  [`examples/rtc`](../../examples/rtc/main.c).
- **Source:** WM_W800 Register Manual v2.1, chapters 20 and 21, checked
  against wm_iot_sdk (`wm_rtc_reg_struct.h`, `wm_pmu_reg_struct.h`,
  `wm_hal_rtc.c`) and wm_sdk_w80x (`wm_rtc.c`, `wm_pmu.c`). v3.0
  (2022-08-20, English and Chinese) has the same content in these chapters.
  The manual is wrong in several places: see [Manual discrepancies](#manual-discrepancies).

> **Status:** the driver follows the two vendor SDKs, which agree with each
> other. Calendar counting, setting the time, the alarm and its interrupt,
> and the clock sources have been checked on a W801 with [`examples/rtc`](../../examples/rtc/main.c)
> *(hardware)*. The points still marked *(to check)* are tested by the
> example but not yet confirmed.

## Registers

Only the PMU registers the RTC uses are described here.

| Offset | Address | Name | SDK | Reset value | Description |
|---|---|---|---|---|---|
| `0x00` | `0x4000_0D00` | [PS_CR](#ps_cr) | `PMU->CTRL` | `0x0000_0062` ¹ | 32 kHz clock source and calibration, sleep/standby |
| `0x04` | `0x4000_0D04` | TIMER0 | `PMU->TIMER0` | `0x0000_0000` | PMU timer 0 (seconds), not covered here |
| `0x08` | `0x4000_0D08` | TIMER1 | `PMU->TIMER1` | `0x0000_0000` | PMU timer 1 (Wi-Fi listen interval), not covered here |
| `0x0C` | `0x4000_0D0C` | [RTC_R1](#rtc_r1) | `PMU->RTC_R1` | `0x0000_0000` | Day, hour, minute, second; alarm enable |
| `0x10` | `0x4000_0D10` | [RTC_R2](#rtc_r2) | `PMU->RTC_R2` | `0x0000_0000` | Year, month; count enable |
| `0x14` | `0x4000_0D14` | [INT_SRC](#int_src) | `PMU->INT_SRC` | `0x0000_0000` | PMU interrupt flags, including the RTC alarm |

¹ The register list says `0x0000_0002`. The bit table gives bit 1 = 1,
bit 5 = 1 and bits 9:6 = 1, which is `0x0000_0062`. A W801 reads
`0x0000_0060` at application start: the bit table's value with bit 1
(sleep_en) clear, as expected on an awake chip. A bootloader runs first, so
this is "what an application starts with" *(hardware)*.

**Contents:** [How it works](#how-it-works) · [Clock source](#clock-source) ·
[Using it](#using-it) · [Registers](#register-details) ·
[Manual discrepancies](#manual-discrepancies)

**Legend:** **RW** read/write, **RO** read-only, **W1C** write 1 to clear.
Notes are tagged by source: *(manual)*, *(SDK)* (vendor SDKs or this SDK's
code), *(hardware)* seen on a real chip, *(to check)* tested by the example
but not yet run.

---

## How it works

- **Calendar.** RTC_R1 holds day, hour, minute and second; RTC_R2 holds year
  and month. The values are plain binary, not BCD as the manual's text says:
  09 seconds counts to 10, not 0x10 *(SDK, hardware)*.
- **Range.** The year field is 7 bits and counts from 2000, so the range is
  2000–2127. Months are 1–12 and days 1–31 *(SDK)*. After 2127-12-31
  23:59:59 it wraps to 2000-01-01 00:00:00 *(hardware)*.
- **Month lengths and leap years** are handled by the hardware: 28, 29, 30
  or 31 days, including the century rule: 2000 is a leap year and 2100 is
  not *(manual, hardware)*.
- **Counting** runs while RTC_R2 bit 16 is 1. Reading R1 and R2 gives the
  current time *(manual, SDK)*.
- **Setting year and month** needs two writes to R2: first year and month
  with bit 16 = 0, then the same value with bit 16 = 1. A single write of
  year, month and bit 16 = 1 starts the counter but leaves year and month at
  0 (written `0x0001_1A0B`, read back `0x0001_0000`) *(hardware)*. wm_iot_sdk
  does it in steps as well *(SDK)*. The manual doesn't mention it.
- **Same registers for time and alarm.** Writing R1/R2 with R1 bit 31 = 0 sets
  the time; writing them with R1 bit 31 = 1 sets the alarm time instead, and
  the counter keeps its time *(SDK, hardware)*. Reads return the current
  time, and R1 bit 31 reads back as the alarm enable *(hardware)*.
  wm_iot_sdk notes that the alarm must be written as whole words *(SDK)*.
- **Alarm.** When the counter reaches the alarm time, INT_SRC bit 4 is set
  (seen on hardware, not bit 5 as the manual says) and PMU_IRQn (29) is
  raised if enabled in the VIC. Write 1 to clear the flag *(SDK, hardware)*.
  An alarm 3 s ahead raised the interrupt after about 3.4 s (it fires on
  the tick where the second matches) *(hardware)*.
- **The compare is "equal", not "at or after".** An alarm set in the past
  never fires; it would fire only when the calendar comes round to that
  exact time again, which can't happen *(hardware)*. Check that the alarm
  time is in the future before you set it, as wm_iot_sdk does.
- **The alarm stays enabled after it fires:** R1 bit 31 is still 1
  afterwards *(hardware)*. That does no harm, because the time it matches
  has passed, but `rtc_alarm_enabled()` keeps returning 1 until
  `rtc_alarm_off()` or `rtc_set_time()` is called.
- **Resets and power.** The RTC is in the always-on PMU, so it keeps counting
  through sleep, standby and (probably) a watchdog reset *(manual,
  to check)*. There is no battery input: the time is lost when power is
  removed.

## Clock source

The RTC counts from the PMU's 32 kHz clock, which has two sources, selected
by [PS_CR](#ps_cr) bit 4 *(manual, SDK)*:

| Bit 4 | Source | Accuracy | In sleep/standby |
|---|---|---|---|
| 0 (reset) | Internal RC oscillator | Drifts with temperature and voltage; calibrate with bit 3 | Keeps running |
| 1 | 40 MHz crystal divided down | As good as the crystal | The crystal stops: the PMU switches back to the RC oscillator and bit 4 is cleared *(manual)* |

- **Calibrating the RC oscillator:** write 0, then 1, to PS_CR bit 3. Both SDKs
  do this at start-up *(manual, SDK)*.
- After waking from sleep, set bit 4 again to go back to the crystal
  *(manual)*.
- `examples/rtc` measures each source against the CPU clock (which comes from
  the crystal). On one W801 at room temperature *(hardware)*:

  | Source | Error |
  |---|---|
  | RC oscillator, not calibrated | about +13.6 % (RTC slow: 5 RTC seconds = 5.68 s) |
  | RC oscillator, calibrated | −270 to −460 ppm (RTC fast), over three runs |
  | 40 MHz crystal / divider | within ±80 ppm of the CPU clock (same crystal; this is mostly measurement error) |

  The uncalibrated RC oscillator is far off, so calibrate it (or use the
  crystal) before relying on the time.

## Using it

```c
#include "w80x_rtc.h"

rtc_init();                                 // start at 2000-01-01 if stopped
struct tm t = rtc_tm(2026, 11, 16, 12, 0, 0);  // year, month 1..12, day, h, m, s
rtc_set_time(&t);                           // -1 if not a valid time
rtc_get_time(&t);                           // struct tm from <time.h>

struct tm a;
rtc_gmtime(rtc_get_unix() + 60, &a);        // one minute later
rtc_set_alarm(&a);
irq_enable(PMU_IRQn);
__enable_irq();

W80X_IRQ void PMU_IRQHandler(void)
{
    if (rtc_alarm_pending()) { rtc_alarm_clear(); /* ... */ }
}
```

- `rtc_get_time()` reads R1, R2 and R1 again, and retries if R1 changed, so a
  carry into a new month or year between the two reads can't give a mixed
  time *(SDK)*.
- `rtc_set_time()` stops the counter, writes R1, writes R2 (year and month)
  and then sets R2 bit 16 in a separate write (see above). It also turns the
  alarm off, because the alarm enable is in R1 *(SDK, hardware)*.
- `rtc_alarm_off()` writes R1 back with bit 31 clear, as wm_sdk_w80x does.
  R1 also holds the time, so this rewrites the current day/hour/minute/second
  *(SDK)*. It keeps the time to within the test's 1 s resolution
  *(hardware)*; whether it loses part of a second is *(to check)*.
- Times are `struct tm` and `time_t` from `<time.h>`, as UTC. `time()` and
  `gettimeofday()` read the RTC (a weak hook in `w80x_stdio.c`).
- Helpers without hardware access: `rtc_tm()`, `rtc_valid()`,
  `rtc_is_leap()`, `rtc_days_in_month()`, `rtc_timegm()` and `rtc_gmtime()`
  (checked on a host against the C library for every hour 2000–2127, and on
  the chip against newlib's `gmtime_r()` by `examples/rtc`).
- `rtc_clock_select(RTC_CLK_XTAL)` / `RTC_CLK_RC32K` and
  `rtc_calibrate_rc32k()` set the clock source.

---

## Register details

### PS_CR

PMU control. Only the bits the RTC uses are described; the sleep and wake-up
bits belong to the low-power modes.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0D00` (base + `0x00`) | 32 bits | RW | `0x0000_0062` ¹ | `PMU->CTRL`, `rtc_clock_select()`, `rtc_calibrate_rc32k()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:11 | — | RO | 0 | Reserved | |
| 10 | wake_neg_sel | RW | 0 | Polarity of the wake-up pin interrupt (sleep only) | |
| 9:6 | wake_thres_level | RW | 1 | Minimum wake-up pin hold time, in 128 ms units | |
| 5 | mux_bg | RW | 1 | DLDO_CORE reference: 1 = ABG, 0 = DBG | |
| 4 | bypass_32k | RW | 0 | 32 kHz clock source. 0: internal RC oscillator · 1: 40 MHz crystal divided down | `PMU_CTRL_BYPASS32K` |
| 3 | cal32k_en | RW | 0 | RC oscillator calibration. 0: calibration circuit in reset · 1: run. Write 0 then 1 to calibrate | `PMU_CTRL_CAL32K` |
| 2 | io_sleep_en | RW | 0 | 1: holding the wake-up pin enters sleep | |
| 1 | sleep_en | RW | 1 ² | Write 1: enter sleep | `PMU_CTRL_SLEEP` |
| 0 | stdby_en | RW | 0 | Write 1: enter standby | `PMU_CTRL_STANDBY` |

² As given by the bit table. A running chip is awake, so this reset value
probably doesn't mean "asleep".

### RTC_R1

Day, hour, minute and second, and the alarm enable. Reading gives the current
time. Writing sets the time (bit 31 = 0) or the alarm time (bit 31 = 1).

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0D0C` (base + `0x0C`) | 32 bits | RW | `0x0000_0000` | `PMU->RTC_R1` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31 | alarm enable | RW | 0 | 0: alarm off · 1: alarm on; the fields written with it are the alarm time | `RTC_R1_ALARM_EN` |
| 30:29 | — | — | 0 | Reserved | |
| 28:24 | day | RW | 0 | Day of the month, 1–31 | `RTC_R1_DAY_Msk` |
| 23:21 | — | — | 0 | Reserved | |
| 20:16 | hour | RW | 0 | 0–23 | `RTC_R1_HOUR_Msk` |
| 15:14 | — | — | 0 | Reserved | |
| 13:8 | minute | RW | 0 | 0–59 (the manual lists these bits as reserved) | `RTC_R1_MIN_Msk` |
| 7:6 | — | — | 0 | Reserved | |
| 5:0 | second | RW | 0 | 0–59 | `RTC_R1_SEC_Msk` |

### RTC_R2

Year and month, and the count enable.

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0D10` (base + `0x10`) | 32 bits | RW | `0x0000_0000` | `PMU->RTC_R2` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:17 | — | — | 0 | Reserved | |
| 16 | count enable | RW | 0 | 0: stopped · 1: counting | `RTC_R2_EN` |
| 15 | — | — | 0 | Reserved | |
| 14:8 | year | RW | 0 | Years since 2000, 0–127 | `RTC_R2_YEAR_Msk` |
| 7:4 | — | — | 0 | Reserved | |
| 3:0 | month | RW | 0 | 1–12 | `RTC_R2_MON_Msk` |

### INT_SRC

PMU interrupt flags, all on PMU_IRQn (29). Bit positions are from both
vendor SDKs; the manual's table differs (see below).

| Address | Size | Access | Reset value | SDK |
|---|---|---|---|---|
| `0x4000_0D14` (base + `0x14`) | 32 bits | RW | `0x0000_0000` | `PMU->INT_SRC`, `rtc_alarm_pending()`, `rtc_alarm_clear()` |

| Bits | Name | Access | Reset | Values | SDK |
|---|---|---|---|---|---|
| 31:9 | — | RO | 0 | Reserved | |
| 8 | last_sleep_state | W1C | 0 | 1: woke up from sleep · 0: power-on or reset | `PMU_INT_SLEEP_WAKE` |
| 7 | standby_status | RO | 0 | 1: woke up from standby | `PMU_INT_STANDBY_WAKE` |
| 6 | osc_timeout | RO | 0 | 1: the 40 MHz oscillator took more than 40 ms to start | `PMU_INT_OSC_TIMEOUT` |
| 5 | dpll_timeout | RO | 0 | 1: the DPLL took more than 2 ms to lock | `PMU_INT_DPLL_TIMEOUT` |
| 4 | rtc_isr_flag | W1C | 0 | 1: the RTC alarm fired | `PMU_INT_RTC` |
| 3 | io_sleep_flag | W1C | 0 | 1: the wake-up pin requested sleep | `PMU_INT_IO_SLEEP` |
| 2 | io_wake_flag | W1C | 0 | 1: woken by the wake-up pin | `PMU_INT_IO_WAKE` |
| 1 | timer1_flag | W1C | 0 | 1: PMU timer 1 expired | `PMU_INT_TIMER1` |
| 0 | timer0_flag | W1C | 0 | 1: PMU timer 0 expired | `PMU_INT_TIMER0` |

Clear only the flags you handle: write those bits as 1 and the rest as 0.
wm_sdk_w80x also writes bits 7 and 8 when it clears a flag *(SDK)*.

---

## Manual discrepancies

All of these are in v2.1 and still in v3.0 (English and Chinese).

| Where | Manual | Vendor SDKs (used here) |
|---|---|---|
| RTC chapter text | "a BCD counter" | Binary fields *(hardware)* |
| RTC_R1 table | Bits 13:8 listed as reserved | Minutes |
| INT_SRC table | RTC flag at bit 5; bits 1, 3 and 4 reserved; bit 8 described twice | RTC flag at bit 4, timer 1 at bit 1, wake-pin sleep request at bit 3 (both SDKs clear the RTC flag with bit 4); the RTC flag is bit 4 *(hardware)* |
| INT_SRC bit 5 text | "0: an interrupt is generated, 1: none" | 1 = flag set (normal polarity) |
| Section 20.3.6 text | Bit 3 switches to the crystal, bit 2 calibrates | Bit 4 switches (`bypass_32k`), bit 3 calibrates; the manual's own PS_CR table agrees with the SDKs |
| PS_CR reset value | Register list `0x0000_0002` | Bit table gives `0x0000_0062`; a W801 reads `0x0000_0060` at app start (sleep bit clear) *(hardware)* |
| RTC_R2 writes | Nothing on write order | Year and month must be written with the count enable at 0, then the enable set in a second write *(hardware)* |
| RTC register list | RTC_R1/R2 at "0x0C/0x10" in a chapter of their own | They are PMU registers: `0x4000_0D0C` / `0x4000_0D10` |
