# Real-time clock (RTC)

[API index](README.md)

- **Source:** [`w80x_rtc.h`](../../w80x/w80x_rtc.h)
- **Registers:** [Real-time clock (RTC)](../mcu/rtc.md)
- **Example:** [`examples/rtc`](../../examples/rtc/main.c)

```c
#include <time.h>
#include "w80x_rtc.h"

rtc_init();                                       // start at 2000-01-01 if stopped, else keep the time
struct tm date = rtc_tm(2026, 11, 16, 12, 0, 0);  // full year, month 1..12, day, h, m, s
rtc_set_time(&date);                              // -1 if date is not a valid time (2000..2127)
rtc_get_time(&date);                              // standard struct tm, tm_wday and tm_yday filled in
time_t now = time(NULL);                          // newlib's time() reads the RTC (UTC)
rtc_set_unix(now);                                // set from Unix time; also rtc_get_unix()

struct tm alarm;
rtc_gmtime(now + 60, &alarm);                     // alarm one minute later
rtc_set_alarm(&alarm);
irq_enable(PMU_IRQn);                             // PMU_IRQHandler: if (rtc_alarm_pending()) rtc_alarm_clear();
__enable_irq();

rtc_clock_select(RTC_CLK_XTAL);                   // 40 MHz crystal / div (accurate) or RTC_CLK_RC32K
```

Times use the standard `<time.h>` types: `struct tm` (`tm_year` = years
since 1900, `tm_mon` = 0..11) and `time_t` (64-bit in newlib). The RTC has no
time zone, so treat it as UTC. `time()` and `gettimeofday()` read it through
a weak hook in `w80x_stdio.c`, to the second; they fail (`time()` returns -1)
while the RTC is stopped. `gmtime_r()` and `strftime()` work as usual.
newlib has no `timegm()`, and its `mktime()` applies the `TZ` time zone and
links the time zone code, so `rtc_timegm()` and `rtc_gmtime()` do the UTC
conversions inline.

The RTC is part of the PMU. It counts through sleep, but not power loss (no
battery input); whether it keeps counting through a watchdog reset is still
to be checked. The alarm compares for equality, so an alarm time that has
already passed never fires. Year and month must be written with the counter
stopped; `rtc_set_time()` does this. See the
[RTC register chapter](../mcu/rtc.md), including the places where the manual
is wrong.
