#pragma once
// =============================================================================
//  sd_logger.h  —  SD card logging and persistence for Tab5 Weather Dashboard
//
//  Uses SPI mode (NOT SDMMC) to avoid conflict with WiFi (SDMMC host 0).
//  SPI pins: SCK=43, MISO=39, MOSI=44, CS=42  @ 25 MHz
//
//  Files written to SD:
//    /sensor/YYYY-MM-DD.csv   — one row per 5-min sensor reading (appended)
//    /config/baseline.json    — IAQ baseline, pressure ref, cumulative runtime
//
//  Call order in setup():
//    sensors_init()  →  sd_init()  →  sd_load_baseline()  →  wifi_begin()
//
//  On boot, sd_load_history() repopulates the 8-hour rolling arrays so graphs
//  are immediately filled rather than starting empty.
// =============================================================================

#include <Arduino.h>

// ── Result of a baseline load ─────────────────────────────────────────────────
struct SdBaseline {
    float    iaq_baseline_ohm;   // 0 = not loaded
    float    pressure_ref_hpa;   // 0 = not loaded
    uint32_t cumulative_s;       // 0 = not loaded
    bool     valid;
};

// ── Result of a history reload ────────────────────────────────────────────────
// Caller passes pre-allocated arrays of SENSOR_HIST_SLOTS floats each.
struct SdHistoryResult {
    int  slots_loaded;   // number of rows actually populated (0–96)
    bool valid;
};

// ── Public API ────────────────────────────────────────────────────────────────

// Call once in setup() BEFORE wifi_begin().
// Returns true if SD card is present and mounted.
// If no card is present the rest of the API becomes no-ops — app runs normally.
bool sd_init();

// Returns true if SD mounted successfully.
bool sd_available();

// Append one CSV row to /sensor/YYYY-MM-DD.csv.
// date_str: "2026-04-05", time_str: "14:35:00+11:00"
// Call every 5 minutes from loop() when a fresh sensor reading is available.
void sd_log_sensor(const char* date_str, const char* time_str,
                   float tempC, float humidity, float pressureHPa,
                   float gasResistanceOhm, float iaqScore, uint8_t iaqAccuracy);

// Write /config/baseline.json — call every 30 minutes and on clean shutdown.
void sd_save_baseline(float iaq_baseline_ohm, float pressure_ref_hpa,
                      uint32_t cumulative_s);

// Read /config/baseline.json — call once after sd_init(), before wifi_begin().
SdBaseline sd_load_baseline();

// Read the last `max_slots` rows from today's CSV (and yesterday's if needed)
// and populate the caller-supplied history arrays in chronological order.
// Returns number of slots actually filled.
// Call once after sd_load_baseline(), before ui_init().
SdHistoryResult sd_load_history(float* out_temp, float* out_hum,
                                float* out_pres, int max_slots,
                                const char* today_date,
                                const char* yesterday_date);

// ── Extended history — read on-demand for 24h / 7d stats screen ──────────────
// Max slots for each window (5-min rows):
#define SD_EXT_SLOTS_24H  288   // 24 hours  × 12 rows/hr
#define SD_EXT_SLOTS_7D   288   // 7 days downsampled to same resolution (every 35 min)

// Result of an extended history read.
// Caller must pass arrays of at least max_slots floats.
struct SdExtResult {
    int   slots_loaded;   // rows actually populated (chronological order)
    bool  valid;
    // Computed stats (from all loaded rows)
    float temp_today_hi,  temp_today_lo,  temp_today_avg;
    float temp_yest_hi,   temp_yest_lo,   temp_yest_avg;
    float temp_7d_hi,     temp_7d_lo,     temp_7d_avg;
    float hum_today_hi,   hum_today_lo,   hum_today_avg;
    float hum_yest_hi,    hum_yest_lo,    hum_yest_avg;
    float hum_7d_hi,      hum_7d_lo,      hum_7d_avg;
    float pres_today_hi,  pres_today_lo,  pres_today_avg;
    float pres_yest_hi,   pres_yest_lo,   pres_yest_avg;
    float pres_7d_hi,     pres_7d_lo,     pres_7d_avg;
    // 7-day trend vs previous 7 days (NaN if insufficient data)
    float temp_7d_vs_prev;
    float hum_7d_vs_prev;
    float pres_7d_vs_prev;
};

// Read up to `window_days` of CSV history from SD into caller arrays.
// Rows are downsampled if needed to fit within `max_slots`.
// date_today: "YYYY-MM-DD" (current date from RTC).
// Also computes all stats fields in the returned SdExtResult.
// Safe to call from loop() — reads SD synchronously, takes ~50–200ms.
SdExtResult sd_read_extended(float* out_temp, float* out_hum, float* out_pres,
                              int max_slots, int window_days,
                              const char* date_today);
