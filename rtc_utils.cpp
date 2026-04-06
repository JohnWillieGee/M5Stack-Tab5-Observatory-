// =============================================================================
//  rtc_utils.cpp  --  RX8130CE RTC via M5Unified (M5.Rtc)
//
//  Approach: configTime syncs the ESP32 system clock to local time directly
//  using the UTC offset. The RTC stores and returns local time. This avoids
//  all UTC/local conversion complexity.
// =============================================================================

#include "rtc_utils.h"
#include "config.h"
#include <M5Unified.h>
#include <time.h>
#include <sys/time.h>

static const char *WEEKDAY_NAMES[] = {
    "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};
static const char *MONTH_NAMES[] = {
    "","January","February","March","April","May","June",
    "July","August","September","October","November","December"
};

// ── Init ──────────────────────────────────────────────────────────────────────
void rtc_init() {
    setenv("TZ", POSIX_TZ, 1);
    tzset();

    m5::rtc_datetime_t rdt;
    if (!M5.Rtc.getDateTime(&rdt) || rdt.date.year < 2024 || rdt.date.year > 2099) {
        Serial.println("[RTC] Date invalid -- NTP sync required.");
        return;
    }

    // RTC holds local time — build a local tm and push to system clock
    struct tm t = {};
    t.tm_year  = rdt.date.year - 1900;
    t.tm_mon   = rdt.date.month - 1;
    t.tm_mday  = rdt.date.date;
    t.tm_hour  = rdt.time.hours;
    t.tm_min   = rdt.time.minutes;
    t.tm_sec   = rdt.time.seconds;
    t.tm_isdst = -1;
    time_t epoch = mktime(&t);   // mktime uses TZ — correct
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);

    struct tm *local = localtime(&epoch);
    Serial.printf("[RTC] Powered up at %04d-%02d-%02d %02d:%02d:%02d %s\n",
        local->tm_year+1900, local->tm_mon+1, local->tm_mday,
        local->tm_hour, local->tm_min, local->tm_sec,
        local->tm_isdst > 0 ? "AEDT" : "AEST");
}

// ── Read — returns local time from system clock ───────────────────────────────
RtcDateTime rtc_now() {
    RtcDateTime r = {};
    time_t now = time(nullptr);
    if (now < 1700000000L) return r;
    struct tm *local = localtime(&now);
    r.year    = local->tm_year + 1900;
    r.month   = local->tm_mon + 1;
    r.day     = local->tm_mday;
    r.hour    = local->tm_hour;
    r.minute  = local->tm_min;
    r.second  = local->tm_sec;
    r.weekday = local->tm_wday;
    return r;
}

// ── Write — stores local time in RTC ─────────────────────────────────────────
void rtc_set(const RtcDateTime &dt) {
    m5::rtc_datetime_t rdt;
    rdt.date.year    = dt.year;
    rdt.date.month   = dt.month;
    rdt.date.date    = dt.day;
    rdt.date.weekDay = dt.weekday;
    rdt.time.hours   = dt.hour;
    rdt.time.minutes = dt.minute;
    rdt.time.seconds = dt.second;
    M5.Rtc.setDateTime(rdt);
}

// ── NTP Sync ──────────────────────────────────────────────────────────────────
bool ntp_sync_and_set_rtc() {
    Serial.println("[NTP] Starting sync...");
    setenv("TZ", POSIX_TZ, 1);
    tzset();
    // Use gmtOffset_sec to get local time directly from NTP
    // AEDT = UTC+11 = 39600s, AEST = UTC+10 = 36000s
    // Use the POSIX TZ string approach with configTime(0,0) + TZ env var
    configTime(0, 0, NTP_SERVER);

    time_t now = 0;
    int attempts = 0;
    while (time(&now) < 1700000000L && attempts++ < 20) delay(500);

    if (time(&now) < 1700000000L) {
        Serial.println("[NTP] Sync failed (timeout)");
        return false;
    }

    // Re-apply TZ after configTime (configTime resets it on some builds)
    setenv("TZ", POSIX_TZ, 1);
    tzset();
    // Force a fresh time read after TZ is set
    now = time(nullptr);
    struct tm *local = localtime(&now);

    Serial.printf("[NTP] Synced: %04d-%02d-%02d %02d:%02d:%02d %s\n",
        local->tm_year+1900, local->tm_mon+1, local->tm_mday,
        local->tm_hour, local->tm_min, local->tm_sec,
        local->tm_isdst > 0 ? "AEDT" : "AEST");

    RtcDateTime dt;
    dt.year    = local->tm_year + 1900;
    dt.month   = local->tm_mon + 1;
    dt.day     = local->tm_mday;
    dt.hour    = local->tm_hour;
    dt.minute  = local->tm_min;
    dt.second  = local->tm_sec;
    dt.weekday = local->tm_wday;
    rtc_set(dt);
    return true;
}

// ── Formatting ────────────────────────────────────────────────────────────────
const char* rtc_time_str(const RtcDateTime &dt) {
    static char buf[12];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
    return buf;
}

const char* rtc_date_str(const RtcDateTime &dt) {
    static char buf[40];
    snprintf(buf, sizeof(buf), "%s %d %s %04d",
        rtc_weekday_name(dt.weekday), dt.day,
        (dt.month >= 1 && dt.month <= 12) ? MONTH_NAMES[dt.month] : "?",
        dt.year);
    return buf;
}

const char* rtc_weekday_name(uint8_t wd) {
    if (wd > 6) return "?";
    return WEEKDAY_NAMES[wd];
}

// ── UTC offset string ─────────────────────────────────────────────────────────
const char* rtc_tz_offset_str() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return (t.tm_isdst > 0) ? "+11:00" : "+10:00";
}
