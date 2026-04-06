// =============================================================================
//  sensors.cpp  —  M5Stack ENV Pro  (BME688 raw driver, open VOC/IAQ algorithm)
//
//  Bosch BME68x library used directly (no BSEC2 — not available for ESP32-P4).
//
//  Open VOC index algorithm (baseline tracking, raspi-bme680-iaq style):
//    1. Heater enabled at 320°C / 150 ms per forced-mode cycle.
//    2. Absolute humidity computed from temp + RH for compensation.
//    3. Humidity-compensated resistance = raw_resistance / exp(HUM_COEFF * abs_hum)
//    4. Baseline resistance adapts slowly toward the highest (cleanest) recent reading.
//    5. IAQ score = (1 - comp_gas / baseline) * 500, clamped 0-500.
//       Lower resistance = more VOCs = higher (worse) score.
//    6. Accuracy: 0=warming up (< 30 min runtime), 1=low (< 4 h),
//                 2=medium (< 24 h), 3=high (>= 24 h cumulative runtime).
//    7. Baseline and cumulative runtime persisted to Preferences every 30 min.
//
//  IAQ classification matches Bosch Table 6:
//    0-50 Excellent, 51-100 Good, 101-150 Lightly polluted,
//    151-200 Moderately polluted, 201-250 Heavily polluted,
//    251-350 Severely polluted, 351+ Extremely polluted.
// =============================================================================

#include "sensors.h"
#include "config.h"
#include <lvgl.h>
#include <Preferences.h>
#include <math.h>

// Timing
#define SAMPLE_INTERVAL_MS    3000UL
#define TREND_INTERVAL_MS     (5UL * 60UL * 1000UL)
#define BASELINE_SAVE_MS      (30UL * 60UL * 1000UL)

// Heater profile (Bosch recommended for IAQ)
#define HEATER_TEMP_C    320
#define HEATER_DURATION  150

// IAQ algorithm constants
#define HUM_COEFF         0.040f
#define BASELINE_RATE     0.01f
#define MIN_VALID_OHM     1000.0f
#define DEFAULT_BASELINE  100000.0f

// Accuracy thresholds (cumulative runtime in seconds)
#define ACC_WARMUP_S      (30 * 60)
#define ACC_LOW_S         (4 * 3600)
#define ACC_MEDIUM_S      (24 * 3600)

// Module statics
static Bme68x     bme;
static SensorData latest      = {};
static bool       initialised = false;
static unsigned long last_sample_ms  = 0;

// Pressure trend ring buffer
static float  p_hist[12]  = {};
static int    p_count     = 0;
static int    p_idx       = 0;
static unsigned long p_last_ms = 0;

// 8-hour sensor history ring buffer
static float  h_temp[SENSOR_HIST_SLOTS]  = {};
static float  h_hum[SENSOR_HIST_SLOTS]   = {};
static float  h_pres[SENSOR_HIST_SLOTS]  = {};
static int    h_idx   = 0;
static int    h_count = 0;
static unsigned long h_last_ms = 0;

// IAQ algorithm state
static float         iaq_baseline_ohm    = DEFAULT_BASELINE;
static unsigned long iaq_cumulative_s    = 0;
static unsigned long iaq_session_start_ms = 0;
static unsigned long iaq_last_save_ms    = 0;

// Forward declarations
static void update_trend(float newPressure);
static void iaq_load_state();
static float iaq_abs_humidity(float tempC, float rh);
static float iaq_compute(float raw_ohm, float tempC, float rh);
static uint8_t iaq_accuracy();

// =============================================================================
//  INIT
// =============================================================================
bool sensors_init(TwoWire &wire) {
    Serial.println("[Sensor] Initialising BME688 (raw driver + open VOC algorithm)...");

    bme.begin(BME688_I2C_ADDR, wire);

    if (bme.checkStatus() == BME68X_ERROR) {
        Serial.printf("[Sensor] BME688 error: %s\n", bme.statusString());
        return false;
    }
    if (bme.checkStatus() == BME68X_WARNING) {
        Serial.printf("[Sensor] BME688 warning: %s\n", bme.statusString());
    }

    // Oversampling: temp x2, pressure x16, humidity x2
    bme.setTPH(BME68X_OS_2X, BME68X_OS_16X, BME68X_OS_2X);

    // Enable heater at 320 degrees C for 150 ms
    bme.setHeaterProf(HEATER_TEMP_C, HEATER_DURATION);

    initialised = true;
    iaq_session_start_ms = millis();

    iaq_load_state();

    Serial.println("[Sensor] BME688 ready (temp/humidity/pressure + open VOC index)");
    Serial.printf("[Sensor] IAQ baseline: %.0f ohm, cumulative runtime: %lu h %02lu min\n",
                  iaq_baseline_ohm,
                  iaq_cumulative_s / 3600,
                  (iaq_cumulative_s % 3600) / 60);
    return true;
}

// =============================================================================
//  LOAD / SAVE IAQ STATE
// =============================================================================
static void iaq_load_state() {
    Preferences prefs;
    prefs.begin("iaq", true);
    iaq_baseline_ohm = prefs.getFloat("baseline", DEFAULT_BASELINE);
    iaq_cumulative_s = prefs.getULong("runtime_s", 0);
    prefs.end();

    if (iaq_baseline_ohm < MIN_VALID_OHM || iaq_baseline_ohm > 2000000.0f)
        iaq_baseline_ohm = DEFAULT_BASELINE;
}

void sensors_save_state() {
    if (!initialised) return;

    unsigned long session_s = (millis() - iaq_session_start_ms) / 1000UL;
    unsigned long total_s   = iaq_cumulative_s + session_s;

    Preferences prefs;
    prefs.begin("iaq", false);
    prefs.putFloat("baseline",  iaq_baseline_ohm);
    prefs.putULong("runtime_s", total_s);
    prefs.end();

    Serial.printf("[Sensor] IAQ state saved: baseline=%.0f ohm, total runtime=%lu h %02lu min\n",
                  iaq_baseline_ohm, total_s / 3600, (total_s % 3600) / 60);
}

// =============================================================================
//  APPLY BASELINE FROM SD
// =============================================================================
void sensors_apply_baseline(float iaq_ohm, float pres_ref, uint32_t cum_s) {
    if (iaq_ohm > MIN_VALID_OHM && iaq_ohm < 2000000.0f)
        iaq_baseline_ohm = iaq_ohm;

    // Pre-seed the pressure trend ring buffer with the reference value
    // so trend calculation has a starting point immediately on boot.
    // Use p_count=1 — one real reference point, not 12 fake ones.
    if (pres_ref > 800.0f && pres_ref < 1100.0f) {
        p_hist[0] = pres_ref;
        p_count   = 1;
        p_idx     = 1;
    }

    if (cum_s > 0) iaq_cumulative_s = cum_s;

    Serial.printf("[Sensor] Baseline applied from SD: iaq=%.0f ohm, "
                  "pref=%.2f hPa, runtime=%lu h %02lu min\n",
                  iaq_ohm, pres_ref,
                  (unsigned long)cum_s / 3600,
                  ((unsigned long)cum_s % 3600) / 60);
}

// =============================================================================
//  APPLY HISTORY FROM SD
// =============================================================================
void sensors_apply_history(const float* temp, const float* hum,
                            const float* pres, int count) {
    if (count <= 0) return;
    if (count > SENSOR_HIST_SLOTS) count = SENSOR_HIST_SLOTS;

    // Load into ring buffer in chronological order starting at slot 0
    for (int i = 0; i < count; i++) {
        h_temp[i] = temp[i];
        h_hum[i]  = hum[i];
        h_pres[i] = pres[i];
    }
    h_count   = count;
    h_idx     = count % SENSOR_HIST_SLOTS;   // next write position
    h_last_ms = millis();                    // suppress immediate re-slot on first run

    // Also pre-seed the pressure trend ring buffer from the last ≤12 slots.
    // This gives update_trend() enough history to compute a slope immediately.
    int trend_n     = (count < 12) ? count : 12;
    int trend_start = count - trend_n;
    for (int i = 0; i < trend_n; i++)
        p_hist[i] = pres[trend_start + i];
    p_count   = trend_n;
    p_idx     = trend_n;
    p_last_ms = 0;   // trigger trend recalc on next sensors_run()

    Serial.printf("[Sensor] History applied from SD: %d slots loaded\n", count);
}

// =============================================================================
//  CUMULATIVE RUNTIME ACCESSOR
// =============================================================================
uint32_t sensors_get_cumulative_s() {
    unsigned long session_s = (millis() - iaq_session_start_ms) / 1000UL;
    return (uint32_t)(iaq_cumulative_s + session_s);
}

// =============================================================================
//  IAQ ALGORITHM HELPERS
// =============================================================================
static float iaq_abs_humidity(float tempC, float rh) {
    return (6.112f * expf((17.67f * tempC) / (tempC + 243.5f)) * rh * 2.1674f)
           / (273.15f + tempC);
}

static float iaq_compute(float raw_ohm, float tempC, float rh) {
    float abs_hum  = iaq_abs_humidity(tempC, rh);
    float comp_gas = raw_ohm / expf(HUM_COEFF * abs_hum);

    // Baseline tracks cleaner (higher) readings faster, polluted slower
    if (comp_gas > iaq_baseline_ohm)
        iaq_baseline_ohm += BASELINE_RATE * (comp_gas - iaq_baseline_ohm);
    else
        iaq_baseline_ohm += (BASELINE_RATE * 0.1f) * (comp_gas - iaq_baseline_ohm);

    float score = (1.0f - (comp_gas / iaq_baseline_ohm)) * 500.0f;
    if (score < 0.0f)   score = 0.0f;
    if (score > 500.0f) score = 500.0f;
    return score;
}

static uint8_t iaq_accuracy() {
    unsigned long session_s = (millis() - iaq_session_start_ms) / 1000UL;
    unsigned long total_s   = iaq_cumulative_s + session_s;
    if (total_s < ACC_WARMUP_S) return 0;
    if (total_s < ACC_LOW_S)    return 1;
    if (total_s < ACC_MEDIUM_S) return 2;
    return 3;
}

// =============================================================================
//  RUN
// =============================================================================
bool sensors_run() {
    if (!initialised) return false;

    unsigned long now = millis();
    if (now - last_sample_ms < SAMPLE_INTERVAL_MS) return false;
    last_sample_ms = now;

    bme.setOpMode(BME68X_FORCED_MODE);
    uint32_t delay_us = bme.getMeasDur(BME68X_FORCED_MODE);
    delay((delay_us / 1000) + 20);

    uint8_t n_fields = bme.fetchData();
    if (n_fields == 0) return false;

    bme68xData data;
    bme.getData(data);

    if (data.status & BME68X_NEW_DATA_MSK) {
        latest.tempC       = data.temperature;
        latest.humidity    = data.humidity;
        latest.pressureHPa = data.pressure / 100.0f;

        bool gas_ok = (data.status & BME68X_GASM_VALID_MSK) &&
                      (data.status & BME68X_HEAT_STAB_MSK)  &&
                      (data.gas_resistance > MIN_VALID_OHM);

        latest.gasValid         = gas_ok;
        latest.gasResistanceOhm = gas_ok ? data.gas_resistance : 0.0f;

        if (gas_ok) {
            latest.iaqScore    = iaq_compute(data.gas_resistance,
                                              data.temperature, data.humidity);
            latest.iaqAccuracy = iaq_accuracy();
        } else {
            latest.iaqAccuracy = 0;
        }

        // Keep legacy aliases in sync
        latest.iaq        = latest.iaqScore;
        latest.staticIaq  = latest.iaqScore;
        latest.co2Equiv   = 0.0f;
        latest.vocEquiv   = 0.0f;
        latest.gasPercent = 0.0f;

        latest.valid = true;

        if (p_last_ms == 0 || (now - p_last_ms) >= TREND_INTERVAL_MS) {
            p_last_ms = now;
            update_trend(latest.pressureHPa);
        }

        if (h_last_ms == 0 || (now - h_last_ms) >= TREND_INTERVAL_MS) {
            h_last_ms = now;
            h_temp[h_idx]  = latest.tempC;
            h_hum[h_idx]   = latest.humidity;
            h_pres[h_idx]  = latest.pressureHPa;
            h_idx = (h_idx + 1) % SENSOR_HIST_SLOTS;
            if (h_count < SENSOR_HIST_SLOTS) h_count++;
        }

        {
            int start = h_count < SENSOR_HIST_SLOTS ? 0 : h_idx;
            for (int i = 0; i < h_count; i++) {
                int si = (start + i) % SENSOR_HIST_SLOTS;
                latest.tempHistory[i]  = h_temp[si];
                latest.humHistory[i]   = h_hum[si];
                latest.pressHistory[i] = h_pres[si];
            }
            latest.histSlotCount = h_count;
        }

        if (iaq_last_save_ms == 0 || (now - iaq_last_save_ms) >= BASELINE_SAVE_MS) {
            iaq_last_save_ms = now;
            sensors_save_state();
        }

        return true;
    }

    return false;
}

// =============================================================================
//  GET
// =============================================================================
SensorData sensors_get() {
    return latest;
}

// =============================================================================
//  PRESSURE TREND
// =============================================================================
static void update_trend(float newPressure) {
    p_hist[p_idx % 12] = newPressure;
    p_idx++;
    if (p_count < 12) p_count++;

    int start = (p_idx - p_count + 24) % 12;
    for (int i = 0; i < p_count; i++)
        latest.pressureHistory[i] = p_hist[(start + i) % 12];
    latest.historyCount = p_count;

    if (p_count < 3) {
        latest.pressureTrend   = SensorData::TREND_UNKNOWN;
        latest.trendRateHPa_hr = 0.0f;
        return;
    }

    float sx = 0, sy = 0, sxy = 0, sxx = 0;
    int n = p_count;
    for (int i = 0; i < n; i++) {
        float xi = (float)i;
        float yi = p_hist[(start + i) % 12];
        sx += xi; sy += yi; sxy += xi * yi; sxx += xi * xi;
    }
    float denom = (n * sxx - sx * sx);
    if (denom == 0) {
        latest.pressureTrend   = SensorData::TREND_STEADY;
        latest.trendRateHPa_hr = 0.0f;
        return;
    }
    float slope = (n * sxy - sx * sy) / denom;
    float rate  = slope * (3600000.0f / (float)TREND_INTERVAL_MS);

    latest.trendRateHPa_hr = rate;
    if      (rate >  0.5f) latest.pressureTrend = SensorData::TREND_RISING;
    else if (rate < -0.5f) latest.pressureTrend = SensorData::TREND_FALLING;
    else                   latest.pressureTrend = SensorData::TREND_STEADY;
}

// =============================================================================
//  LABEL HELPERS
// =============================================================================
const char* sensors_iaq_label(float iaq, uint8_t accuracy) {
    if (accuracy == 0)                        return "Warming up";
    if (iaq <= IAQ_EXCELLENT)                 return "Excellent";
    if (iaq <= IAQ_GOOD)                      return "Good";
    if (iaq <= IAQ_LIGHTLY_POLLUTED)          return "Lightly polluted";
    if (iaq <= IAQ_MODERATELY_POLLUTED)       return "Moderately polluted";
    if (iaq <= IAQ_HEAVILY_POLLUTED)          return "Heavily polluted";
    if (iaq <= IAQ_SEVERELY_POLLUTED)         return "Severely polluted";
    return "Extremely polluted";
}

const char* sensors_accuracy_label(uint8_t accuracy) {
    switch (accuracy) {
        case 0:  return "Warming up";
        case 1:  return "Low accuracy";
        case 2:  return "Calibrating";
        case 3:  return "High accuracy";
        default: return "--";
    }
}

float sensors_get_baseline() { return iaq_baseline_ohm; }

const char* sensors_trend_string(SensorData::Trend t) {
    switch (t) {
        case SensorData::TREND_RISING:  return LV_SYMBOL_UP   " Rising";
        case SensorData::TREND_FALLING: return LV_SYMBOL_DOWN " Falling";
        case SensorData::TREND_STEADY:  return "  Steady";
        default:                        return "--";
    }
}
