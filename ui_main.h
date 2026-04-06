#pragma once
// =============================================================================
//  ui_main.h  —  LVGL UI  (10 screens, touch navigation)
//  1280 × 720 MIPI-DSI display
// =============================================================================

#include <lvgl.h>
#include "rtc_utils.h"
#include "sensors.h"
#include "weather_api.h"
#include "space_weather_api.h"
#include "astro_api.h"
#include "bom_warnings_api.h"
#include "sd_logger.h"
#include "earthquake_api.h"

// ── Screen indices ────────────────────────────────────────────────────────────
#define SCREEN_CLOCK       0
#define SCREEN_LOCAL       1   // ENV Pro live readings (BME688)
#define SCREEN_WEATHER     2   // WeatherAPI current conditions
#define SCREEN_FORECAST    3   // 3-day forecast
#define SCREEN_ASTRONOMY   4   // Sunrise/sunset, moon phase — includes sun arc
#define SCREEN_SOLAR       5   // Solar elevation chart (full-day curve)
#define SCREEN_SEASONS     6   // Earth orbital seasons diagram
#define SCREEN_LUNAR       7   // Lunar orbit + phase wheel
#define SCREEN_PLANETS     8   // AstronomyAPI planet visibility + compass
#define SCREEN_ALERTS      9   // WeatherAPI active warnings/alerts
#define SCREEN_SPACE       10  // BOM Space Weather (K/A/Dst indices + aurora)
#define SCREEN_SENSOR_HIST 11  // Sensor Stats — tabbed temp/hum/pressure, 8h/24h/7d
#define SCREEN_TIDES       12  // Fort Denison tides — read from /tides.csv on SD card
#define SCREEN_EARTHQUAKE  13  // USGS earthquake activity — Australia / Pacific
#define SCREEN_SETTINGS    14  // Settings + system info  (excluded from swipe)
#define SCREEN_COUNT       15  // total screens (14 swipeable 0-13 + settings=14)

// ── Init — call once in setup() after lvgl_init() ────────────────────────────
void ui_init();

// ── Update functions — called from loop() ────────────────────────────────────
void ui_update_clock(const RtcDateTime &dt);
void ui_update_local(const SensorData &sd);
// Sensor Stats screen (Screen 11) — tabbed temp/hum/pressure with 8h/24h/7d windows.
// Call on every sensor read (passes live SensorData for 8h history + current values).
// date_today: "YYYY-MM-DD" from RTC — used for on-demand SD extended reads.
void ui_update_sensor_stats(const SensorData &sd, const char* date_today);
void ui_update_weather(const WeatherResult &wr);
// Alerts screen — call with BOM result (primary) + WeatherResult (fallback).
// If bom.valid is true, BOM data is used regardless of count.
// If bom.valid is false (fetch failed), falls back to wr.alerts[].
void ui_update_alerts(const BomWarningsResult &bom, const WeatherResult &wr);
void ui_update_space_weather(const SpaceWeatherResult &sw);
void ui_update_planets(const AstroResult &ar);      // planets tonight screen
void ui_update_settings_sysinfo();  // refresh battery/wifi/memory on settings screen
void ui_settings_sync_highlights(); // sync button highlights to current setting values
void ui_update_tides(const RtcDateTime &dt); // tides screen — reads /tides.csv from SD
void ui_update_earthquake(const EqResult &eq); // earthquake screen — USGS feed

// ── Display timeout / wake ────────────────────────────────────────────────────
void ui_notify_touch();             // resets inactivity timer; wakes display if off
void ui_apply_brightness(int b);    // apply brightness + update settings highlight
void ui_request_wx_refresh();       // flag loop to force weather fetch immediately
void ui_request_sw_refresh();       // flag loop to force space wx fetch immediately

// ── Settings live values — written by settings screen, read by loop() ─────────
extern volatile int  g_setting_brightness;
extern volatile int  g_setting_timeout_sec;     // 0 = never
extern volatile bool g_setting_dim_before_sleep;
extern volatile int  g_setting_wx_interval_sec;
extern volatile int  g_setting_sw_interval_sec;
extern volatile bool g_setting_wake_to_clock;
extern volatile bool g_force_wx_refresh;
extern volatile bool g_force_sw_refresh;
void ui_update_statusbar();           // WiFi icon, battery, clock, RSSI, bat%
void ui_update_statusbar_sensors(const SensorData &sd);  // temp/hum/press in statusbar

// ── Celestial screens update from RTC date + astronomy API data ──────────────
// Call whenever the date changes (daily) or when astro data is refreshed.
void ui_update_celestial(const RtcDateTime &dt, const WeatherResult &wr);

// ── Navigate to a screen ─────────────────────────────────────────────────────
void ui_show_screen(int screen_id);
int  ui_current_screen();
