// =============================================================================
//  Tab5 Weather Dashboard  —  Arduino IDE  (M5Unified + LVGL)
//  Hardware : M5Stack Tab5  (ESP32-P4, 1280×720 MIPI-DSI display)
//  Sensor   : M5Stack ENV Pro  (BME688 raw driver — temp, humidity, pressure)
//             NOTE: BSEC2 not used — no RISC-V binary for ESP32-P4 yet.
//  RTC      : RX8130CE  (via M5Unified M5.Rtc)
//  Author   : <your name>
//  Started  : 2025
// =============================================================================
//
//  PROJECT STRUCTURE
//  -----------------
//  Tab5_WeatherDash.ino     <- this file  (setup / loop / hardware init)
//  config.h                 <- WiFi, API keys, timezone, setting defaults
//  sensors.h / .cpp         <- ENV Pro BME688 raw driver
//  rtc_utils.h / .cpp       <- RX8130CE helpers + NTP sync
//  weather_api.h / .cpp     <- WeatherAPI.com HTTP fetch + JSON parse
//  space_weather_api.h/.cpp <- BOM Space Weather Services API
//  ui_main.h / .cpp         <- LVGL screen creation + update helpers
//
//  WHY M5Unified (not plain M5GFX):
//  The Tab5 ESP32-P4 requires M5Unified's M5.begin() to power-sequence the DSI
//  peripheral, PMIC, and I2C expander before any display call is made. Using plain
//  M5GFX without this causes a ROM-level Load Access Fault (MCAUSE 0x05,
//  MTVAL 0x20) because DSI registers aren't powered yet. M5.begin() handles
//  all of this correctly and is required even for M5GFX-only display access.
//
//  SCREENS (swipe, tap arrows, or long-press for quick-pick menu)
//  --------------------------------------------------------------
//  0  Clock / RTC
//  1  Local Conditions  (BME688: temp, humidity, pressure + trend)
//  2  Weather Current
//  3  Forecast 3-day
//  4  Astronomy
//  5  Seasons
//  6  Lunar Orbit
//  7  Tides  (BOM Fort Denison — 7-day turning points + cosine chart)
//  8  Space Weather (BOM SWS — K/A/Dst + Aurora alerts)
//  9  Settings & System Info  (via quick-pick menu or swipe)
//  10 Solar Elevation
//  11 Sensor History
//  12 Tides — Fort Denison (reads /tides.csv from SD card)
// =============================================================================

#include <Arduino.h>
#include <M5Unified.h>      // MUST be first — handles all Tab5 hardware init
#include "esp_log.h"        // for esp_log_level_set (suppress I2C spam)
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// LVGL — install "lvgl" by LVGL in Arduino Library Manager (v8.x)
#include <lvgl.h>

// ── Project headers ──────────────────────────────────────────────────────────
#include "config.h"
#include "sensors.h"
#include "space_weather_api.h"
#include "bom_warnings_api.h"
#include "astro_api.h"
#include "rtc_utils.h"
#include "weather_api.h"
#include "ui_main.h"
#include "sd_logger.h"
#include "earthquake_api.h"

// =============================================================================
//  LVGL RENDER BUFFERS  (allocated in PSRAM in lvgl_init)
// =============================================================================
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

// =============================================================================
//  LVGL CALLBACKS
// =============================================================================

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    // setSwapBytes(true) tells M5GFX to byte-swap RGB565 data from LVGL.
    // LV_COLOR_16_SWAP is kept 0 — M5GFX handles the swap natively here.
    M5.Display.setSwapBytes(true);
    M5.Display.pushImage(area->x1, area->y1, w, h, (uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    // getTouchRaw is the correct LVGL callback method for Tab5
    // M5.Touch.getDetail() needs M5.update() which conflicts in this context
    lgfx::touch_point_t tp[3];
    uint8_t n = M5.Lcd.getTouchRaw(tp, 3);
    if (n > 0) {
        // Raw touch is portrait (720x1280). Display is rotation 3 (landscape 1280x720).
        // Transform: mirror both axes for correct swipe direction
        data->state   = LV_INDEV_STATE_PR;
        data->point.x = (1280 - 1) - tp[0].y;
        data->point.y = tp[0].x;
        // Notify UI of touch — resets inactivity timer and wakes display
        ui_notify_touch();
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// =============================================================================
//  LVGL INIT
// =============================================================================

static void lvgl_init() {
    lv_init();

    // Render buffers in PSRAM — 1/20th screen each (~90 KB per buffer)
    size_t buf_px = M5.Display.width() * M5.Display.height() / 20;
    buf1 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_px * sizeof(lv_color_t),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf1 || !buf2) {
        Serial.println("[LVGL] PSRAM alloc failed — falling back to internal RAM");
        heap_caps_free(buf1); heap_caps_free(buf2);
        buf_px = M5.Display.width() * 4;
        buf1   = (lv_color_t *)malloc(buf_px * sizeof(lv_color_t));
        buf2   = nullptr;
    }

    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t      disp_drv;
    static lv_indev_drv_t     indev_drv;

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_px);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = M5.Display.width();
    disp_drv.ver_res  = M5.Display.height();
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Set black background NOW — safe because a display driver is registered
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);

    Serial.printf("[LVGL] Display: %d x %d\n",
                  M5.Display.width(), M5.Display.height());
}

// =============================================================================
//  WIFI
// =============================================================================

void wifi_begin() {
    WiFi.setPins(WIFI_CLK_PIN, WIFI_CMD_PIN,
                 WIFI_D0_PIN, WIFI_D1_PIN, WIFI_D2_PIN, WIFI_D3_PIN,
                 WIFI_RST_PIN);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] Connecting to %s …\n", WIFI_SSID);
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

// =============================================================================
//  UPDATE TIMERS
// =============================================================================

static unsigned long t_rtc       = 0;
static unsigned long t_celestial = 0;
static unsigned long t_weather   = 0;
static unsigned long t_space     = 0;
static unsigned long t_astro     = 0;   // AstronomyAPI — fetch every 6 hours
static unsigned long t_sysinfo   = 0;   // settings sysinfo refresh (5 s)
static unsigned long t_bom_warn  = 0;   // BOM warnings — fetch same cadence as weather
static unsigned long t_sd_log      = 0; // SD sensor CSV row — every 5 min
static unsigned long t_sd_baseline = 0; // SD baseline.json save — every 30 min
static unsigned long t_tides       = 0; // tides screen refresh — every 60 s
static unsigned long t_eq          = 0; // earthquake feed — every 15 min

static unsigned long t_ntp       = 0;
static bool          ntp_synced  = false;
static WeatherResult       g_last_wr = {};
static SpaceWeatherResult  g_last_sw = {};
static AstroResult         g_last_ar = {};
static BomWarningsResult   g_last_bom_warn = {};
static EqResult            g_last_eq = {};

// Last-fetch timestamps for display on settings screen (millis)
unsigned long g_t_weather_last = 0;
unsigned long g_t_space_last   = 0;
unsigned long g_t_astro_last   = 0;
unsigned long g_t_ntp_last     = 0;

// =============================================================================
//  SETUP
// =============================================================================

// Increase loopTask stack — WeatherResult + DynamicJsonDocument need headroom
SET_LOOP_TASK_STACK_SIZE(16 * 1024);   // 16 KB (default is 8 KB)

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[Tab5] Weather Dashboard booting…");

    // ── M5Unified init — MUST be first ───────────────────────────────────────
    // Handles Tab5 DSI power sequencing, PMIC, I2C expander, display, and touch.
    // Disable unused subsystems to save init time and avoid conflicts.
    // Suppress noisy I2C NACK errors from ESP-IDF log — they spam the monitor
    // when sensors are not connected. Remove this line to debug I2C issues.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    auto cfg = M5.config();
    cfg.internal_imu = false;   // BMI270 not needed
    cfg.internal_mic = false;   // ES7210 not needed
    cfg.internal_spk = false;   // ES8311 not needed
    M5.begin(cfg);

    // Tab5 with M5Unified: rotation 3 = landscape with correct orientation
    M5.Display.setRotation(3);

    // ── Load all settings from Preferences (namespace "dash") ────────────────
    {
        Preferences prefs;
        prefs.begin("dash", true);   // read-only
        g_setting_brightness       = prefs.getInt ("bright",   DEFAULT_BRIGHTNESS);
        g_setting_timeout_sec      = prefs.getInt ("timeout",  DEFAULT_TIMEOUT_SEC);
        g_setting_dim_before_sleep = prefs.getBool("dim",      DEFAULT_DIM_BEFORE_SLEEP);
        g_setting_wx_interval_sec  = prefs.getInt ("wx_int",   DEFAULT_WX_INTERVAL_SEC);
        g_setting_sw_interval_sec  = prefs.getInt ("sw_int",   DEFAULT_SW_INTERVAL_SEC);
        g_setting_wake_to_clock    = prefs.getBool("wake_clk", DEFAULT_WAKE_TO_CLOCK);
        prefs.end();
    }
    M5.Display.setBrightness(g_setting_brightness);
    Serial.printf("[Display] %d x %d  brightness=%d  timeout=%ds\n",
                  M5.Display.width(), M5.Display.height(),
                  (int)g_setting_brightness, (int)g_setting_timeout_sec);

    // ── LVGL ─────────────────────────────────────────────────────────────────
    lvgl_init();

    // ── ENV Pro sensor (BME688) — Tab5 Grove J11 CON4 ───────────────────────
    // J11 pin3=GPIO53(SDA), pin4=GPIO54(SCL)
    // SYS_EXT5VO is enabled by M5.begin() — no extra call needed
    // (setExtOutput with masks crashes on Tab5 power expander)
    // Grove J11: SDA=GPIO53, SCL=GPIO54 (confirmed from Tab5 schematic)
    // Must use Wire.begin() directly — TwoWire(1) does not work on ESP32-P4
    Wire.begin(53, 54, 100000);
    delay(50);
    sensors_init(Wire);

    // ── RTC (RX8130CE) — handled by M5.Rtc (set up in M5.begin) ───────────────
    rtc_init();

    // ── SD card — MUST be before wifi_begin() ────────────────────────────────
    // SPI mode bypasses SDMMC host entirely — no conflict with WiFi.
    // Pins: SCK=43, MISO=39, MOSI=44, CS=42 @ 25 MHz (official M5Stack Tab5 docs).
    if (sd_init()) {
        // Load IAQ + pressure baseline from SD — overrides Preferences defaults
        SdBaseline bl = sd_load_baseline();
        if (bl.valid) {
            sensors_apply_baseline(bl.iaq_baseline_ohm,
                                   bl.pressure_ref_hpa,
                                   bl.cumulative_s);
        }

        // Reload 8-hour sensor history so graphs are populated immediately on boot
        RtcDateTime now_dt = rtc_now();
        char today[12], yesterday[12];
        snprintf(today, sizeof(today), "%04d-%02d-%02d",
                 now_dt.year, now_dt.month, now_dt.day);

        // Compute yesterday's date via mktime day-roll
        struct tm t = {};
        t.tm_year  = now_dt.year - 1900;
        t.tm_mon   = now_dt.month - 1;
        t.tm_mday  = now_dt.day - 1;   // mktime normalises day=0 to prev month
        t.tm_isdst = -1;
        time_t yest_epoch = mktime(&t);
        struct tm *yest   = localtime(&yest_epoch);
        snprintf(yesterday, sizeof(yesterday), "%04d-%02d-%02d",
                 yest->tm_year + 1900, yest->tm_mon + 1, yest->tm_mday);

        float sd_temp[SENSOR_HIST_SLOTS] = {};
        float sd_hum[SENSOR_HIST_SLOTS]  = {};
        float sd_pres[SENSOR_HIST_SLOTS] = {};
        SdHistoryResult hr = sd_load_history(sd_temp, sd_hum, sd_pres,
                                             SENSOR_HIST_SLOTS,
                                             today, yesterday);
        if (hr.valid) {
            sensors_apply_history(sd_temp, sd_hum, sd_pres, hr.slots_loaded);
        }
    }

    // ── WiFi ─────────────────────────────────────────────────────────────────
    wifi_begin();

    // ── Build LVGL UI ────────────────────────────────────────────────────────
    ui_init();

    // Sync settings button highlights to the values just loaded from Preferences
    ui_settings_sync_highlights();

    // Prime LVGL — render first screen fully before loop() begins
    for (int i = 0; i < 10; i++) {
        lv_timer_handler();
        delay(10);
    }

    Serial.println("[Tab5] Setup complete.");
}

// =============================================================================
//  LOOP
// =============================================================================

void loop() {
    M5.update();   // polls touch, buttons, power events

    // ── LVGL tick + handler ──────────────────────────────────────────────────
    lv_tick_inc(5);          // fixed 5ms tick matches delay(5) at end of loop
    lv_timer_handler();
    unsigned long now_ms = millis();

    // ── Screen timeout / dim logic ───────────────────────────────────────────
    // s_last_touch_ms, s_display_dimmed, s_display_off live in ui_main.cpp.
    // ui_notify_touch() is called from lvgl_touch_cb above on every press.
    if (g_setting_timeout_sec > 0) {
        extern unsigned long s_last_touch_ms;
        extern bool          s_display_dimmed;
        extern bool          s_display_off;

        unsigned long ref_ms = (s_last_touch_ms == 0) ? now_ms : s_last_touch_ms;
        unsigned long idle_ms    = now_ms - ref_ms;
        unsigned long timeout_ms = (unsigned long)g_setting_timeout_sec * 1000UL;
        // Dim phase: 30 s before blank (only if dim_before_sleep is on)
        unsigned long dim_ms = (timeout_ms > 30000UL)
                               ? timeout_ms - 30000UL : timeout_ms;

        if (!s_display_off && idle_ms >= timeout_ms) {
            M5.Display.setBrightness(0);
            s_display_off    = true;
            s_display_dimmed = false;
        } else if (!s_display_off && !s_display_dimmed
                   && g_setting_dim_before_sleep && idle_ms >= dim_ms) {
            M5.Display.setBrightness(15);   // 6% — visible warning
            s_display_dimmed = true;
        }
    }

    // ── Force-refresh flags (set by Settings screen Refresh Now buttons) ─────
    if (g_force_wx_refresh) {
        g_force_wx_refresh = false;
        t_weather = 0;   // will trigger on next loop iteration
    }
    if (g_force_sw_refresh) {
        g_force_sw_refresh = false;
        t_space = 0;
    }

    // ── 1-second clock / RTC + status bar ───────────────────────────────────
    if (now_ms - t_rtc >= 1000UL) {
        t_rtc = now_ms;
        RtcDateTime dt = rtc_now();
        ui_update_clock(dt);
        ui_update_statusbar();
    }

    // ── 60-second celestial canvas redraw + tide countdowns ─────────────────
    if (now_ms - t_celestial >= 60000UL || t_celestial == 0) {
        t_celestial = now_ms;
        ui_update_celestial(rtc_now(), g_last_wr);
    }

    // ── 5-second settings / sysinfo refresh ──────────────────────────────────
    if (now_ms - t_sysinfo >= 5000UL || t_sysinfo == 0) {
        t_sysinfo = now_ms;
        ui_update_settings_sysinfo();
    }

    // ── ENV Pro sensor read (~3 s cycle internally) ──────────────────────────
    if (sensors_run()) {
        SensorData sd = sensors_get();
        if (sd.valid) {
            ui_update_local(sd);

            // Sensor stats screen — pass live data + today's date for SD extended reads
            {
                RtcDateTime _dt = rtc_now();
                char _date[12];
                snprintf(_date, sizeof(_date), "%04d-%02d-%02d",
                         _dt.year, _dt.month, _dt.day);
                ui_update_sensor_stats(sd, _date);
            }

            // ── SD: log one CSV row every 5 minutes ──────────────────────────
            if (sd_available() &&
                (t_sd_log == 0 || now_ms - t_sd_log >= 5UL * 60UL * 1000UL)) {
                t_sd_log = now_ms;
                RtcDateTime rdt = rtc_now();
                char date_str[12], time_str[20];
                snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
                         rdt.year, rdt.month, rdt.day);
                snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d%s",
                         rdt.hour, rdt.minute, rdt.second,
                         rtc_tz_offset_str());
                sd_log_sensor(date_str, time_str,
                              sd.tempC, sd.humidity, sd.pressureHPa,
                              sd.gasResistanceOhm, sd.iaqScore, sd.iaqAccuracy);
            }

            // ── SD: save baseline.json every 30 minutes ───────────────────────
            if (sd_available() &&
                (t_sd_baseline == 0 || now_ms - t_sd_baseline >= 30UL * 60UL * 1000UL)) {
                t_sd_baseline = now_ms;
                sd_save_baseline(sensors_get_baseline(),
                                 sd.pressureHPa,
                                 sensors_get_cumulative_s());
            }
        }
    }

    // ── Tides screen — refresh every 60 s (reads SD card, no network) ────────
    if (sd_available() &&
        (t_tides == 0 || now_ms - t_tides >= 60000UL)) {
        t_tides = now_ms;
        ui_update_tides(rtc_now());
    }

    // ── USGS Earthquake feed — every 15 minutes ───────────────────────────────
    {
        const unsigned long EQ_INTERVAL_MS = 15UL * 60UL * 1000UL;
        if (wifi_is_connected() &&
            (t_eq == 0 || now_ms - t_eq >= EQ_INTERVAL_MS)) {
            t_eq = now_ms;
            EqResult eq = eq_fetch();
            if (eq.valid) {
                g_last_eq = eq;
            }
            // Always update screen — shows "no data" state if fetch failed
            ui_update_earthquake(g_last_eq);
        }
    }

    // ── NTP sync on first WiFi connect then every 6 hours ───────────────────
    if (wifi_is_connected()) {
        if (!ntp_synced || (now_ms - t_ntp >= 6UL * 3600UL * 1000UL)) {
            t_ntp      = now_ms;
            ntp_synced = ntp_sync_and_set_rtc();
            if (ntp_synced) g_t_ntp_last = now_ms;
        }
    }

    // ── WeatherAPI fetch — interval from settings ─────────────────────────────
    {
        unsigned long wx_ms = (unsigned long)g_setting_wx_interval_sec * 1000UL;
        if (wifi_is_connected() && (t_weather == 0 || now_ms - t_weather >= wx_ms)) {
            t_weather = now_ms;
            WeatherResult wr = weather_fetch();
            if (wr.valid) {
                g_last_wr       = wr;
                g_t_weather_last = now_ms;
                ui_update_weather(wr);
                t_celestial = 0;   // trigger immediate celestial redraw
            }

            // BOM warnings — fetch on same cadence as weather, right after
            // Use WeatherAPI result as fallback if BOM fails
            BomWarningsResult bom_warn = bom_warnings_fetch();
            if (bom_warn.valid) {
                g_last_bom_warn  = bom_warn;
                t_bom_warn       = now_ms;
            }
            // Always call ui_update_alerts — passes both sources, UI decides which to show
            ui_update_alerts(g_last_bom_warn, g_last_wr);
        }
    }

    // ── BOM Space Weather fetch — interval from settings ──────────────────────
    {
        unsigned long sw_ms = (unsigned long)g_setting_sw_interval_sec * 1000UL;
        if (wifi_is_connected() && (t_space == 0 || now_ms - t_space >= sw_ms)) {
            t_space = now_ms;
            SpaceWeatherResult sw = space_weather_fetch();
            if (sw.valid) {
                g_last_sw     = sw;
                g_t_space_last = now_ms;
                ui_update_space_weather(sw);
            }
        }
    }

    // ── AstronomyAPI planet positions — fetch every 6 hours ──────────────────
    // Query at 21:00 local time (good evening viewing time for Sydney).
    // On first boot fetches immediately; thereafter every 6 hours.
    {
        const unsigned long ASTRO_INTERVAL_MS = 6UL * 3600UL * 1000UL;
        if (wifi_is_connected() &&
            (t_astro == 0 || now_ms - t_astro >= ASTRO_INTERVAL_MS)) {
            t_astro = now_ms;

            // Build date string from RTC (YYYY-MM-DD) and fixed viewing time
            RtcDateTime rdt = rtc_now();
            char date_str[12], time_str[12];
            snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
                     rdt.year, rdt.month, rdt.day);
            // Use 21:00 local as the query time — good evening viewing
            snprintf(time_str, sizeof(time_str), "21:00:00");

            AstroResult ar = astro_fetch(date_str, time_str);
            if (ar.valid) {
                g_last_ar      = ar;
                g_t_astro_last = now_ms;
                ui_update_planets(ar);
            }
        }
    }

    delay(5);
}
