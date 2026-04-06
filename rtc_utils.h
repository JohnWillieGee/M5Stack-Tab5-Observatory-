#pragma once
// =============================================================================
//  rtc_utils.h  --  RX8130CE RTC direct I2C driver (no M5Unified)
//  RX8130CE I2C address: 0x32
//  Registers: sec=0x00 min=0x01 hr=0x02 wday=0x03 day=0x04 mon=0x05 yr=0x06
// =============================================================================

#include <Arduino.h>
#include <Wire.h>

// ── Datetime struct ───────────────────────────────────────────────────────────
struct RtcDateTime {
    uint16_t year;
    uint8_t  month;    // 1-12
    uint8_t  day;      // 1-31
    uint8_t  hour;     // 0-23
    uint8_t  minute;   // 0-59
    uint8_t  second;   // 0-59
    uint8_t  weekday;  // 0=Sun .. 6=Sat
};

// ── Public API ────────────────────────────────────────────────────────────────
void rtc_init();
RtcDateTime rtc_now();
void rtc_set(const RtcDateTime &dt);
bool ntp_sync_and_set_rtc();

const char* rtc_time_str(const RtcDateTime &dt);
const char* rtc_date_str(const RtcDateTime &dt);
const char* rtc_weekday_name(uint8_t wd);

// Returns the current UTC offset string for local AEST/AEDT time.
// e.g. "+11:00" during daylight saving, "+10:00" otherwise.
const char* rtc_tz_offset_str();
