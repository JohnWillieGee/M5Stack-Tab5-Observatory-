#pragma once
// =============================================================================
//  sensors.h  —  M5Stack ENV Pro  (BME688 raw driver — no BSEC2)
//
//  Uses Bosch's open-source BME68x Arduino library directly.
//  BSEC2 is not available for the ESP32-P4 RISC-V architecture.
//
//  Provides: temperature, humidity, pressure, pressure trend, and a
//  VOC/air-quality index derived from the raw gas resistance using an
//  open baseline-tracking algorithm (no BSEC2 required).
//
//  VOC index algorithm (raspi-bme680-iaq style):
//    - Heater at 320°C / 150 ms (Bosch recommended IAQ profile)
//    - Absolute humidity compensation applied to raw resistance
//    - Slowly-adapting baseline resistance tracked via Preferences
//    - IAQ score = (1 - comp_gas / baseline) * 500, clamped 0-500
//    - Accuracy: 0=warming up, 1=low (< 30 min), 2=medium, 3=high (> 24 h)
//    - Accuracy levels reflect cumulative runtime stored in Preferences
//
//  Library required (Arduino Library Manager):
//    "Bosch BME68x Library"  by Bosch Sensortec
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <bme68xLibrary.h>   // Bosch open-source BME68x driver

// ── VOC/IAQ quality bands (Bosch Table 6 classification) ─────────────────────
#define IAQ_EXCELLENT        50
#define IAQ_GOOD            100
#define IAQ_LIGHTLY_POLLUTED 150
#define IAQ_MODERATELY_POLLUTED 200
#define IAQ_HEAVILY_POLLUTED 250
#define IAQ_SEVERELY_POLLUTED 350

// ── Sensor data bundle ────────────────────────────────────────────────────────
struct SensorData {
    // Temperature & humidity (raw BME688 — slightly warmer than ambient
    // due to chip self-heating; typically +1 to +2°C offset)
    float    tempC;
    float    humidity;

    // Pressure
    float    pressureHPa;

    // ── Open VOC/IAQ algorithm outputs ───────────────────────────────────────
    float    gasResistanceOhm;  // raw gas resistance in Ohms (0 if heater not stable)
    float    iaqScore;          // 0 (clean) to 500 (heavily polluted)
    uint8_t  iaqAccuracy;       // 0=warming up, 1=low, 2=medium, 3=high
    bool     gasValid;          // true when heater stable and resistance valid

    // Legacy fields — kept so any other references compile; always 0
    float    iaq;               // alias for iaqScore (set equal)
    float    staticIaq;
    float    co2Equiv;
    float    vocEquiv;
    float    gasPercent;

    // Pressure trend
    // NOTE: RISING/FALLING clash with Arduino.h macros — use TREND_ prefix
    enum Trend { TREND_UNKNOWN, TREND_RISING, TREND_STEADY, TREND_FALLING } pressureTrend;
    float    pressureHistory[12];   // last 12 readings (1/5 min = 1 hour)
    int      historyCount;
    float    trendRateHPa_hr;

    // 8-hour rolling history — 96 slots at 5-minute cadence
    #define SENSOR_HIST_SLOTS  96
    float    tempHistory[SENSOR_HIST_SLOTS];
    float    humHistory[SENSOR_HIST_SLOTS];
    float    pressHistory[SENSOR_HIST_SLOTS];
    int      histSlotCount;

    bool     valid;
};

// ── Public API ────────────────────────────────────────────────────────────────

// Call once in setup() — wire must already be begun
bool sensors_init(TwoWire &wire);

// Call from loop() every iteration.
// Returns true when a fresh BME688 reading is available (~3 s cycle).
bool sensors_run();

// Returns the latest SensorData
SensorData sensors_get();

// Persist IAQ baseline to Preferences (call every 30 min from loop)
void sensors_save_state();

// Human-readable VOC/IAQ label based on score + accuracy
const char* sensors_iaq_label(float iaq, uint8_t accuracy);

// Human-readable accuracy string
const char* sensors_accuracy_label(uint8_t accuracy);

// Human-readable pressure trend string
const char* sensors_trend_string(SensorData::Trend t);

// Returns current IAQ baseline resistance in ohms (for debug display)
float sensors_get_baseline();

// Returns cumulative IAQ runtime in seconds (session + previously stored)
uint32_t sensors_get_cumulative_s();

// Apply baseline values loaded from SD card on boot.
// Call once after sd_load_baseline(), before the first sensors_run().
void sensors_apply_baseline(float iaq_baseline_ohm,
                             float pressure_ref_hpa,
                             uint32_t cumulative_s);

// Apply history arrays loaded from SD card on boot.
// Populates the internal ring buffer so graphs are filled immediately.
void sensors_apply_history(const float* temp, const float* hum,
                            const float* pres, int count);
