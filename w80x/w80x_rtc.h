/*
 * w80x_rtc.h - Real-time clock (manual chapter 21, part of the PMU). Header-only.
 *
 * The RTC is a calendar counter in the PMU: year (2000..2127), month, day,
 * hour, minute and second, plus one alarm that raises PMU_IRQn. It counts
 * from the PMU's 32 kHz clock: the internal RC oscillator (default), or the
 * 40 MHz crystal divided down (more accurate, see rtc_clock_select()). The
 * hardware handles month lengths and leap years. See docs/mcu/rtc.md.
 *
 * Times are the standard C types from <time.h>: struct tm (tm_year = years
 * since 1900, tm_mon = 0..11) and time_t (Unix time, 64 bits in newlib).
 * The RTC has no time zone; treat it as UTC.
 *
 *     rtc_init();                                  // starts it if stopped
 *     struct tm date = rtc_tm(2026, 11, 16, 12, 0, 0);
 *     rtc_set_time(&date);
 *     rtc_get_time(&date);                         // also fills tm_wday, tm_yday
 *     time_t now = time(NULL);                     // newlib's time() reads the RTC
 *
 * Alarm:
 *
 *     W80X_IRQ void PMU_IRQHandler(void)
 *     {
 *         if (rtc_alarm_pending()) { rtc_alarm_clear(); alarm_fired = 1; }
 *     }
 *     ...
 *     struct tm alarm;
 *     rtc_gmtime(rtc_get_unix() + 10, &alarm);     // 10 s from now
 *     rtc_set_alarm(&alarm);
 *     irq_enable(PMU_IRQn);
 *     __enable_irq();
 *
 * newlib has no timegm(), and its mktime() applies the TZ time zone (and
 * links the time zone code), so rtc_timegm() and rtc_gmtime() do the UTC
 * conversions inline. gmtime_r(), strftime() and friends work as usual.
 *
 * The time is lost on power-off (there is no battery domain).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef W80X_RTC_H
#define W80X_RTC_H

#include <time.h>
#include "w80x.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTC_YEAR_MIN 2000
#define RTC_YEAR_MAX 2127

/* ---- calendar helpers (pure C, no hardware) ---------------------------- */

/* year is the full year (2026). */
W80X_INLINE int rtc_is_leap(int year)
{
    return ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
}

/* Days in month mon (0..11, as tm_mon) of the full year. */
W80X_INLINE int rtc_days_in_month(int year, int mon)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if ((mon < 0) || (mon > 11)) {
        return 0;
    }
    return days[mon] + (((mon == 1) && rtc_is_leap(year)) ? 1 : 0);
}

/* 1 if tm is a date and time the RTC can hold (tm_wday, tm_yday and tm_isdst
 * are ignored). */
W80X_INLINE int rtc_valid(const struct tm *tm)
{
    int year = tm->tm_year + 1900;

    return (year >= RTC_YEAR_MIN) && (year <= RTC_YEAR_MAX) &&
           (tm->tm_mday >= 1) && (tm->tm_mday <= rtc_days_in_month(year, tm->tm_mon)) &&
           (tm->tm_hour >= 0) && (tm->tm_hour < 24) && (tm->tm_min >= 0) && (tm->tm_min < 60) &&
           (tm->tm_sec >= 0) && (tm->tm_sec < 60);
}

/* Days since 1970-01-01 for a full year, month 1..12 and day (Howard
 * Hinnant's days_from_civil, years >= 1970; the short names are his). */
W80X_INLINE uint32_t rtc__days(uint32_t y, uint32_t m, uint32_t d)
{
    y -= (m <= 2u) ? 1u : 0u;
    uint32_t era = y / 400u, yoe = y - (era * 400u);
    uint32_t doy = (((153u * ((m > 2u) ? (m - 3u) : (m + 9u))) + 2u) / 5u) + d - 1u;
    uint32_t doe = (yoe * 365u) + (yoe / 4u) - (yoe / 100u) + doy;
    return (era * 146097u) + doe - 719468u;
}

/* Set tm_wday, tm_yday and tm_isdst (0) from the date in tm. */
W80X_INLINE void rtc__fill(struct tm *tm)
{
    uint32_t year = (uint32_t)(tm->tm_year + 1900);
    uint32_t days = rtc__days(year, (uint32_t)tm->tm_mon + 1u, (uint32_t)tm->tm_mday);

    tm->tm_wday  = (int)((days + 4u) % 7u);          /* 1970-01-01 was a Thursday */
    tm->tm_yday  = (int)(days - rtc__days(year, 1u, 1u));
    tm->tm_isdst = 0;
}

/* A struct tm from human values: full year, month 1..12, day 1..31. tm_wday
 * and tm_yday are filled in. The values are not checked (see rtc_valid()). */
W80X_INLINE struct tm rtc_tm(int year, int month, int day, int hour, int min, int sec)
{
    struct tm tm = { 0 };

    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = sec;
    if ((year >= 1970) && (month >= 1) && (month <= 12) && (day >= 1)) {
        rtc__fill(&tm);
    }
    return tm;
}

/* struct tm (UTC) to Unix time, like BSD timegm(): only the date and time
 * fields are used, and they must be in range (years >= 1970). */
W80X_INLINE time_t rtc_timegm(const struct tm *tm)
{
    uint32_t days = rtc__days((uint32_t)(tm->tm_year + 1900), (uint32_t)tm->tm_mon + 1u,
                              (uint32_t)tm->tm_mday);

    return ((time_t)days * 86400) + (tm->tm_hour * 3600) + (tm->tm_min * 60) + tm->tm_sec;
}

/* Unix time to struct tm (UTC), like gmtime_r(), for unix_time >= 0. */
W80X_INLINE void rtc_gmtime(time_t unix_time, struct tm *tm)
{
    uint32_t z   = (uint32_t)(unix_time / 86400) + 719468u;
    uint32_t rem = (uint32_t)(unix_time % 86400);
    uint32_t era = z / 146097u, doe = z - (era * 146097u);
    uint32_t yoe = (doe - (doe / 1460u) + (doe / 36524u) - (doe / 146096u)) / 365u;
    uint32_t doy = doe - ((365u * yoe) + (yoe / 4u) - (yoe / 100u));
    uint32_t mp  = ((5u * doy) + 2u) / 153u;
    uint32_t m   = (mp < 10u) ? (mp + 3u) : (mp - 9u);

    tm->tm_year = (int)(yoe + (era * 400u) + ((m <= 2u) ? 1u : 0u)) - 1900;
    tm->tm_mon  = (int)m - 1;
    tm->tm_mday = (int)(doy - (((153u * mp) + 2u) / 5u) + 1u);
    tm->tm_hour = (int)(rem / 3600u);
    tm->tm_min  = (int)((rem / 60u) % 60u);
    tm->tm_sec  = (int)(rem % 60u);
    rtc__fill(tm);
}

/* ---- hardware ----------------------------------------------------------- */

W80X_INLINE uint32_t rtc__r1(const struct tm *tm)
{
    return ((uint32_t)tm->tm_sec << RTC_R1_SEC_Pos) | ((uint32_t)tm->tm_min << RTC_R1_MIN_Pos) |
           ((uint32_t)tm->tm_hour << RTC_R1_HOUR_Pos) | ((uint32_t)tm->tm_mday << RTC_R1_DAY_Pos);
}

W80X_INLINE uint32_t rtc__r2(const struct tm *tm)
{
    return ((uint32_t)(tm->tm_mon + 1) << RTC_R2_MON_Pos) |
           ((uint32_t)(tm->tm_year + 1900 - RTC_YEAR_MIN) << RTC_R2_YEAR_Pos);
}

W80X_INLINE int rtc_is_running(void) { return (PMU->RTC_R2 & RTC_R2_EN) != 0; }

/* Read the current time, with tm_wday and tm_yday. R1 is read before and
 * after R2, so a carry from 23:59:59 into the next month or year can't give
 * a mixed reading. */
W80X_INLINE void rtc_get_time(struct tm *tm)
{
    uint32_t r1, r2;

    do {
        r1 = PMU->RTC_R1;
        r2 = PMU->RTC_R2;
    } while (((r1 ^ PMU->RTC_R1) & ~RTC_R1_ALARM_EN) != 0u);

    tm->tm_sec  = (int)((r1 & RTC_R1_SEC_Msk) >> RTC_R1_SEC_Pos);
    tm->tm_min  = (int)((r1 & RTC_R1_MIN_Msk) >> RTC_R1_MIN_Pos);
    tm->tm_hour = (int)((r1 & RTC_R1_HOUR_Msk) >> RTC_R1_HOUR_Pos);
    tm->tm_mday = (int)((r1 & RTC_R1_DAY_Msk) >> RTC_R1_DAY_Pos);
    tm->tm_mon  = (int)((r2 & RTC_R2_MON_Msk) >> RTC_R2_MON_Pos) - 1;
    tm->tm_year = (int)((r2 & RTC_R2_YEAR_Msk) >> RTC_R2_YEAR_Pos) + RTC_YEAR_MIN - 1900;
    if (rtc_valid(tm)) {
        rtc__fill(tm);
    } else {
        tm->tm_wday  = -1;                   /* registers hold no valid date */
        tm->tm_yday  = -1;
        tm->tm_isdst = 0;
    }
}

/* Current time as Unix time (UTC). */
W80X_INLINE time_t rtc_get_unix(void)
{
    struct tm tm;

    rtc_get_time(&tm);
    return rtc_timegm(&tm);
}

/* Set the time and start counting. Returns 0, or -1 if tm is not valid.
 * tm_wday, tm_yday and tm_isdst are ignored. This also turns the alarm off
 * (the alarm enable is in the same register).
 *
 * Year and month must be written with the count enable at 0, then the enable
 * set in a second write: a single write of year/month together with the
 * enable starts the counter but leaves year and month at 0 (seen on a W801;
 * wm_iot_sdk also writes them in separate steps). */
W80X_INLINE int rtc_set_time(const struct tm *tm)
{
    if (!rtc_valid(tm)) {
        return -1;
    }
    PMU->RTC_R2 = PMU->RTC_R2 & ~RTC_R2_EN; /* stop counting */
    PMU->RTC_R1 = rtc__r1(tm);
    PMU->RTC_R2 = rtc__r2(tm);              /* year and month, still stopped */
    PMU->RTC_R2 = rtc__r2(tm) | RTC_R2_EN;  /* start */
    return 0;
}

/* Set the time from Unix time (UTC). Returns 0, or -1 if it is outside
 * 2000..2127. */
W80X_INLINE int rtc_set_unix(time_t unix_time)
{
    struct tm tm;

    if (unix_time < 0) {
        return -1;
    }
    rtc_gmtime(unix_time, &tm);
    return rtc_set_time(&tm);
}

/* Start the RTC if it is stopped (power-on), at 2000-01-01 00:00:00. Keeps
 * the time if it is already counting (for example after a watchdog reset).
 * Returns 1 if it was already running, 0 if it was started. */
W80X_INLINE int rtc_init(void)
{
    if (rtc_is_running()) {
        return 1;
    }
    struct tm epoch = rtc_tm(RTC_YEAR_MIN, 1, 1, 0, 0, 0);
    rtc_set_time(&epoch);
    return 0;
}

/* ---- alarm -------------------------------------------------------------- */

/* Set the alarm: PMU_INT_RTC is set (and PMU_IRQn raised, if enabled) when
 * the RTC reaches tm. Writes both registers as whole words, R1 first, the way
 * wm_iot_sdk does (its notes say the alarm must be written by word). Leaves
 * the current time alone. Returns 0, or -1 if tm is not valid (tm_wday,
 * tm_yday and tm_isdst are ignored).
 *
 * The hardware compares for equality: an alarm time that has already passed
 * never fires. The enable stays set after the alarm fires (R1 bit 31 reads
 * 1) until rtc_alarm_off() or rtc_set_time(). */
W80X_INLINE int rtc_set_alarm(const struct tm *tm)
{
    if (!rtc_valid(tm)) {
        return -1;
    }
    PMU->INT_SRC = PMU_INT_RTC;             /* drop an old alarm */
    PMU->RTC_R1  = rtc__r1(tm) | RTC_R1_ALARM_EN;
    PMU->RTC_R2  = rtc__r2(tm) | RTC_R2_EN;
    return 0;
}

/* Turn the alarm off. R1 also holds the time, so this writes back the current
 * day/hour/minute/second without the alarm enable (as wm_sdk_w80x does). */
W80X_INLINE void rtc_alarm_off(void)
{
    PMU->RTC_R1 = PMU->RTC_R1 & ~RTC_R1_ALARM_EN;
}

W80X_INLINE int  rtc_alarm_enabled(void) { return (PMU->RTC_R1 & RTC_R1_ALARM_EN) != 0; }
W80X_INLINE int  rtc_alarm_pending(void) { return (PMU->INT_SRC & PMU_INT_RTC) != 0; }
W80X_INLINE void rtc_alarm_clear(void)   { PMU->INT_SRC = PMU_INT_RTC; }

/* ---- 32 kHz clock source ------------------------------------------------- */

typedef enum {
    RTC_CLK_RC32K = 0,      /* internal RC oscillator: works in sleep, drifts   */
    RTC_CLK_XTAL  = 1,      /* 40 MHz crystal divided down: accurate, awake only */
} rtc_clk_t;

/* Select the PMU 32 kHz clock (it also clocks the PMU timers). The manual
 * says the PMU falls back to the RC oscillator in sleep. */
W80X_INLINE void rtc_clock_select(rtc_clk_t src)
{
    if (src == RTC_CLK_XTAL) {
        PMU->CTRL |= PMU_CTRL_BYPASS32K;
    } else {
        PMU->CTRL &= ~PMU_CTRL_BYPASS32K;
    }
}

W80X_INLINE rtc_clk_t rtc_clock_source(void)
{
    return (PMU->CTRL & PMU_CTRL_BYPASS32K) ? RTC_CLK_XTAL : RTC_CLK_RC32K;
}

/* Calibrate the RC oscillator (write 0, then 1, to the calibration bit). */
W80X_INLINE void rtc_calibrate_rc32k(void)
{
    PMU->CTRL &= ~PMU_CTRL_CAL32K;
    PMU->CTRL |= PMU_CTRL_CAL32K;
}

#ifdef __cplusplus
}
#endif

#endif /* W80X_RTC_H */
