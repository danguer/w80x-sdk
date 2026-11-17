/*
 * rtc - real-time clock self-test and clock (w80x_rtc.h).
 *
 * Board: HLK-W801-KIT-V1.1. Output on UART0, 115200 baud (`make monitor`).
 *
 * At start it runs tests that also check where the manual is unclear (see
 * docs/mcu/rtc.md): binary vs BCD counting, month lengths and leap years
 * (including 2100), the alarm and its interrupt flag bit, an alarm set in
 * the past, turning the alarm off, and how far each 32 kHz clock source is
 * from the CPU's crystal clock. Then it prints the time every second.
 *
 * Keys: s = set the time (type YYYY-MM-DD HH:MM:SS, Enter)
 *       a = alarm in 5 s     c = switch clock source (RC 32K / crystal)
 *       k = calibrate RC 32K  w = watchdog reset (the RTC keeps counting)
 *       t = run the tests again (they change the time)
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "w80x.h"
#include "w80x_rtc.h"
#include "w80x_uart.h"
#include "w80x_utils.h"
#include "w80x_wdg.h"
#include "boards/hlk_w801_kit.h"

static volatile uint32_t alarm_irqs;        /* PMU_IRQHandler saw the RTC flag */
static volatile uint32_t alarm_int_src;     /* INT_SRC when it did */
static unsigned passed, failed;

W80X_IRQ void PMU_IRQHandler(void)
{
    uint32_t src = PMU->INT_SRC;

    if (src & PMU_INT_RTC) {
        alarm_int_src = src;
        alarm_irqs++;
    }
    PMU->INT_SRC = src & (PMU_INT_TIMER0 | PMU_INT_TIMER1 | PMU_INT_IO_WAKE |
                          PMU_INT_IO_SLEEP | PMU_INT_RTC);
}

/* ---- CPU-cycle time base (CORET wraps every ~0.2 s: poll often) -------- */

static uint64_t cyc_total;
static uint32_t cyc_last;

static void cyc_start(void)
{
    cyc_last  = CPU_TICKS;
    cyc_total = 0;
}

static uint64_t cyc_now(void)
{
    cyc_total += cpu_cycles_step(&cyc_last);
    return cyc_total;
}

/* Milliseconds since cyc_start(). cpu_cycles_to_ms() takes 32 bits, which
 * covers 17.8 s at 240 MHz; the waits here are at most 5 s. */
static uint32_t cyc_ms(void)
{
    return cpu_cycles_to_ms((uint32_t)cyc_now());
}

/* Wait until the seconds change (or timeout_ms passes). Returns 0 on timeout. */
static int wait_tick(struct tm *t, uint32_t timeout_ms)
{
    struct tm a, b;

    rtc_get_time(&a);
    cyc_start();
    do {
        rtc_get_time(&b);
        if (b.tm_sec != a.tm_sec) {
            *t = b;
            return 1;
        }
    } while (cyc_ms() < timeout_ms);
    *t = b;
    return 0;
}

/* ---- printing and checks ----------------------------------------------- */

static void fmt_time(char *buf, size_t n, const struct tm *t)
{
    static const char *wd[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d %s", t->tm_year + 1900, t->tm_mon + 1,
             t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec,
             ((t->tm_wday >= 0) && (t->tm_wday < 7)) ? wd[t->tm_wday] : "?");
}

/* Same date and time, and the same weekday and day of the year. */
static int same(const struct tm *a, const struct tm *b)
{
    return (a->tm_year == b->tm_year) && (a->tm_mon == b->tm_mon) && (a->tm_mday == b->tm_mday) &&
           (a->tm_hour == b->tm_hour) && (a->tm_min == b->tm_min) && (a->tm_sec == b->tm_sec) &&
           (a->tm_wday == b->tm_wday) && (a->tm_yday == b->tm_yday);
}

static void check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) {
        passed++;
    } else {
        failed++;
    }
}

static void print_regs(const char *when)
{
    printf("  %s: PS_CR=0x%08lx INT_SRC=0x%08lx RTC_R1=0x%08lx RTC_R2=0x%08lx\n", when,
           (unsigned long)PMU->CTRL, (unsigned long)PMU->INT_SRC,
           (unsigned long)PMU->RTC_R1, (unsigned long)PMU->RTC_R2);
}

/* Set `from`, wait for the next tick and compare with `to`. */
static void rollover(struct tm from, struct tm to, const char *what)
{
    struct tm t;
    char got[40], want[40], line[120];

    rtc_set_time(&from);
    int ticked = wait_tick(&t, 2500);
    fmt_time(got, sizeof got, &t);
    fmt_time(want, sizeof want, &to);
    snprintf(line, sizeof line, "%-28s -> %s%s%s", what, got,
             same(&t, &to) ? "" : "  (expected ", same(&t, &to) ? "" : want);
    if (!same(&t, &to)) {
        snprintf(line + strlen(line), sizeof line - strlen(line), ")%s", ticked ? "" : " no tick");
    }
    check(ticked && same(&t, &to), line);
}

/* rtc_timegm() / rtc_gmtime() against newlib's gmtime_r(), every 7 h 13 min
 * 17 s from 2000 to the end of 2127 (about 156,000 times). */
static void check_conversions(void)
{
    struct tm a, b;
    unsigned bad = 0, n = 0;
    time_t end = rtc_timegm(&(struct tm){ .tm_year = 228, .tm_mon = 0, .tm_mday = 1 });

    for (time_t s = rtc_timegm(&(struct tm){ .tm_year = 100, .tm_mon = 0, .tm_mday = 1 });
         s < end; s += (7 * 3600) + (13 * 60) + 17) {
        rtc_gmtime(s, &a);
        gmtime_r(&s, &b);
        if (!same(&a, &b) || (rtc_timegm(&a) != s)) {
            bad++;
        }
        n++;
    }
    printf("  rtc_gmtime/rtc_timegm vs gmtime_r: %u times, %u different\n", n, bad);
    check((bad == 0u) && (n > 100000u), "rtc_gmtime() and rtc_timegm() agree with newlib");
}

/* Measure the RTC second against the CPU clock (from the 40 MHz crystal). */
static void measure(const char *name, unsigned secs)
{
    struct tm t;

    wait_tick(&t, 2500);                /* align to a second edge */
    cyc_start();
    for (unsigned i = 0; i < secs; i++) {
        uint64_t base = cyc_now();
        do {
            struct tm u;
            rtc_get_time(&u);
            if (u.tm_sec != t.tm_sec) {
                t = u;
                break;
            }
        } while ((cyc_now() - base) < (uint64_t)SystemCoreClock * 2u);
    }
    uint64_t cyc = cyc_now();
    int64_t ppm = (((int64_t)cyc - ((int64_t)SystemCoreClock * secs)) * 1000000) /
                  ((int64_t)SystemCoreClock * secs);
    printf("  %-22s %u RTC seconds = %lu.%03lu s of CPU time (%ld ppm%s)\n", name, secs,
           (unsigned long)(cyc / SystemCoreClock),
           (unsigned long)(((cyc % SystemCoreClock) * 1000u) / SystemCoreClock),
           (long)ppm, (ppm > 0) ? ", RTC slow" : ((ppm < 0) ? ", RTC fast" : ""));
}

static void tests(void)
{
    struct tm t, u;
    char s[40];

    passed = 0;
    failed = 0;
    printf("\n== RTC tests ==\n");

    /* set and read back */
    struct tm t0 = rtc_tm(2026, 11, 16, 12, 0, 9);
    rtc_set_time(&t0);
    rtc_get_time(&t);
    check(same(&t, &t0) && rtc_is_running(), "set 2026-11-16 12:00:09 and read it back");
    print_regs("after set");
    time_t now = time(NULL);
    check((now == rtc_get_unix()) && (now == rtc_timegm(&t0)), "time() returns the RTC time");
    check(rtc_set_time(&(struct tm){ .tm_year = 99, .tm_mon = 11, .tm_mday = 31 }) != 0,
          "rtc_set_time() rejects 1999");
    check_conversions();

    /* counting format and carries */
    printf("Rollovers (binary counting, carries, month lengths, leap years):\n");
    rollover(rtc_tm(2026, 11, 16, 12, 0, 9), rtc_tm(2026, 11, 16, 12, 0, 10),  "09 -> 10 s (binary, not BCD)");
    rollover(rtc_tm(2026, 11, 16, 12, 0, 59), rtc_tm(2026, 11, 16, 12, 1, 0),   "minute carry");
    rollover(rtc_tm(2026, 11, 16, 12, 59, 59), rtc_tm(2026, 11, 16, 13, 0, 0),   "hour carry");
    rollover(rtc_tm(2026, 11, 16, 23, 59, 59), rtc_tm(2026, 11, 17, 0, 0, 0),    "day carry");
    rollover(rtc_tm(2026, 4, 30, 23, 59, 59), rtc_tm(2026, 5, 1, 0, 0, 0),      "30-day month (April)");
    rollover(rtc_tm(2026, 1, 31, 23, 59, 59), rtc_tm(2026, 2, 1, 0, 0, 0),      "31-day month (January)");
    rollover(rtc_tm(2026, 2, 28, 23, 59, 59), rtc_tm(2026, 3, 1, 0, 0, 0),      "Feb 28, 2026 (not leap)");
    rollover(rtc_tm(2024, 2, 28, 23, 59, 59), rtc_tm(2024, 2, 29, 0, 0, 0),     "Feb 28, 2024 (leap)");
    rollover(rtc_tm(2024, 2, 29, 23, 59, 59), rtc_tm(2024, 3, 1, 0, 0, 0),      "Feb 29, 2024");
    rollover(rtc_tm(2000, 2, 28, 23, 59, 59), rtc_tm(2000, 2, 29, 0, 0, 0),     "Feb 28, 2000 (leap, /400)");
    rollover(rtc_tm(2100, 2, 28, 23, 59, 59), rtc_tm(2100, 3, 1, 0, 0, 0),      "Feb 28, 2100 (not leap)");
    rollover(rtc_tm(2026, 12, 31, 23, 59, 59), rtc_tm(2027, 1, 1, 0, 0, 0),      "year carry");

    /* last second of the range: informational */
    rtc_set_time(&(struct tm){ .tm_year = 227, .tm_mon = 11, .tm_mday = 31, .tm_hour = 23, .tm_min = 59, .tm_sec = 59 });
    wait_tick(&t, 2500);
    fmt_time(s, sizeof s, &t);
    printf("  [info] 2127-12-31 23:59:59 (last year) -> %s (R2=0x%08lx)\n", s, (unsigned long)PMU->RTC_R2);

    /* alarm */
    printf("Alarm:\n");
    struct tm noon = rtc_tm(2026, 11, 16, 12, 0, 0);
    rtc_set_time(&noon);
    wait_tick(&t, 2500);                       /* now 12:00:01, just after the tick */
    struct tm al;
    rtc_gmtime(rtc_timegm(&t) + 3, &al);
    alarm_irqs = 0;
    alarm_int_src = 0;
    rtc_alarm_clear();
    irq_enable(PMU_IRQn);
    rtc_set_alarm(&al);
    rtc_get_time(&u);
    check(same(&u, &t), "setting the alarm leaves the time alone");
    check(rtc_alarm_enabled(), "RTC_R1 bit 31 reads back 1 (alarm enabled)");
    print_regs("alarm set");
    cyc_start();
    while ((alarm_irqs == 0u) && (cyc_ms() < 5000u)) {
    }
    uint32_t ms = cyc_ms();
    rtc_get_time(&u);
    fmt_time(s, sizeof s, &u);
    printf("  alarm IRQ after %lu ms at %s, INT_SRC=0x%08lx\n", (unsigned long)ms, s,
           (unsigned long)alarm_int_src);
    check((alarm_irqs != 0u) && (ms >= 2000u) && (ms <= 3500u), "alarm 3 s ahead fires after 2..3.5 s");
    check(alarm_int_src & BIT(4), "RTC flag is INT_SRC bit 4 (manual: bit 5)");
    print_regs("after alarm");
    rtc_alarm_off();

    /* alarm in the past: does it fire at once (>= compare) or never (== compare)? */
    rtc_get_time(&t);
    rtc_gmtime(rtc_timegm(&t) - 60, &al);   /* a minute ago */
    alarm_irqs = 0;
    rtc_set_alarm(&al);
    cyc_start();
    while ((alarm_irqs == 0u) && (cyc_ms() < 2000u)) {
    }
    printf("  [info] alarm set 1 minute in the past: %s\n",
           alarm_irqs ? "fires at once (compare is >=)" : "does not fire within 2 s (compare is ==)");
    rtc_alarm_off();

    /* alarm off */
    rtc_get_time(&t);
    rtc_gmtime(rtc_timegm(&t) + 2, &al);
    rtc_set_alarm(&al);
    rtc_alarm_off();
    rtc_get_time(&u);
    check(!rtc_alarm_enabled() && ((rtc_timegm(&u) - rtc_timegm(&t)) <= 1),
          "rtc_alarm_off() turns it off and keeps the time");
    alarm_irqs = 0;
    cyc_start();
    while ((alarm_irqs == 0u) && (cyc_ms() < 3500u)) {
    }
    check(alarm_irqs == 0u, "no alarm after rtc_alarm_off()");
    irq_disable(PMU_IRQn);

    /* accuracy of each clock source */
    printf("Clock sources (CPU clock from the 40 MHz crystal is the reference):\n");
    rtc_clk_t was = rtc_clock_source();
    rtc_clock_select(RTC_CLK_RC32K);
    measure("RC 32K", 5);
    rtc_calibrate_rc32k();
    measure("RC 32K, calibrated", 5);
    rtc_clock_select(RTC_CLK_XTAL);
    measure("40 MHz crystal / div", 5);
    rtc_clock_select(was);

    printf("== %u passed, %u failed ==\n\n", passed, failed);
}

/* Read "YYYY-MM-DD HH:MM:SS" from the console. */
static void set_from_console(void)
{
    char line[32];
    unsigned n = 0;
    unsigned y, mo, d, h, mi, se;

    printf("time (YYYY-MM-DD HH:MM:SS): ");
    for (;;) {
        int c = uart_getc(UART0);
        if ((c == '\r') || (c == '\n')) {
            break;
        }
        if (((c == 8) || (c == 127)) && (n > 0u)) {
            n--;
            printf("\b \b");
        } else if ((c >= ' ') && (n < (sizeof line - 1u))) {
            line[n++] = (char)c;
            putchar(c);
        }
        fflush(stdout);
    }
    line[n] = 0;
    printf("\n");
    if (sscanf(line, "%u-%u-%u %u:%u:%u", &y, &mo, &d, &h, &mi, &se) == 6) {
        struct tm t = rtc_tm((int)y, (int)mo, (int)d, (int)h, (int)mi, (int)se);
        printf("%s\n", (rtc_set_time(&t) == 0) ? "set" : "not a valid time (2000..2127)");
    } else {
        printf("format: 2026-11-16 12:00:00\n");
    }
}

int main(void)
{
    uart0_init(115200);
    stdio_set_uart(UART0);
    __enable_irq();                     /* PMU_IRQn is enabled in the VIC by the alarm tests */

    printf("\n\nrtc on the %s\n", BOARD_NAME);
    struct tm t;
    char s[40];
    int was_running = rtc_is_running();
    rtc_get_time(&t);
    fmt_time(s, sizeof s, &t);
    printf("At boot: RTC %s, time %s, clock %s, reset status 0x%lx\n",
           was_running ? "running" : "stopped", s,
           (rtc_clock_source() == RTC_CLK_XTAL) ? "crystal" : "RC 32K",
           (unsigned long)reset_status());
    print_regs("at boot");
    reset_status_clear();

    if (was_running) {
        printf("RTC already running (kept through the reset): skipping the tests, 't' runs them\n");
    } else {
        rtc_init();
        tests();
        rtc_set_unix(rtc_timegm(&(struct tm){ .tm_year = 126, .tm_mon = 0, .tm_mday = 1 }));
    }

    printf("Keys: s = set time, a = alarm in 5 s, c = clock source, k = calibrate RC,\n"
           "      w = watchdog reset, t = tests\n");
    irq_enable(PMU_IRQn);
    int last = -1;
    for (;;) {
        rtc_get_time(&t);
        if (t.tm_sec != last) {
            last = t.tm_sec;
            fmt_time(s, sizeof s, &t);
            printf("%s  %s%s\n", s, (rtc_clock_source() == RTC_CLK_XTAL) ? "xtal" : "rc32k",
                   rtc_alarm_enabled() ? "  alarm on" : "");
        }
        if (alarm_irqs != 0u) {
            alarm_irqs = 0;
            rtc_alarm_off();
            printf("*** ALARM ***\n");
        }
        int c = uart_try_getc(UART0);
        if (c == 's') {
            set_from_console();
        } else if (c == 'a') {
            rtc_get_time(&t);
            rtc_gmtime(rtc_get_unix() + 5, &t);
            rtc_set_alarm(&t);
            fmt_time(s, sizeof s, &t);
            printf("alarm at %s\n", s);
        } else if (c == 'c') {
            rtc_clock_select((rtc_clock_source() == RTC_CLK_XTAL) ? RTC_CLK_RC32K : RTC_CLK_XTAL);
        } else if (c == 'k') {
            rtc_calibrate_rc32k();
            printf("RC 32K calibrated\n");
        } else if (c == 'w') {
            printf("watchdog reset...\n");
            uart_flush(UART0);
            wdg_system_reset();
        } else if (c == 't') {
            irq_disable(PMU_IRQn);
            tests();
            irq_enable(PMU_IRQn);
        }
    }
}
