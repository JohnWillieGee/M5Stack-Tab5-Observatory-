// =============================================================================
//  sd_logger.cpp  —  SD card logging and persistence
//
//  SPI mode — bypasses SDMMC host entirely, no conflict with WiFi.
//  Official M5Stack Tab5 pin assignments (from M5Stack Arduino docs):
//    SCK=43, MISO=39, MOSI=44, CS=42 @ 25 MHz
// =============================================================================

#include "sd_logger.h"
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>

#define SD_CS_PIN   42
#define SD_SCK_PIN  43
#define SD_MOSI_PIN 44
#define SD_MISO_PIN 39
#define SD_FREQ_HZ  25000000

static bool s_available = false;

// =============================================================================
//  INIT
// =============================================================================
bool sd_init() {
    Serial.println("[SD] Initialising (SPI mode)...");

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN, SPI, SD_FREQ_HZ)) {
        Serial.println("[SD] No card detected or init failed — SD logging disabled.");
        s_available = false;
        return false;
    }

    // Create directories if they don't exist
    if (!SD.exists("/sensor")) SD.mkdir("/sensor");
    if (!SD.exists("/config")) SD.mkdir("/config");

    uint64_t cardMB = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD] Card mounted — %.0llu MB, type %d\n", cardMB, SD.cardType());
    s_available = true;
    return true;
}

bool sd_available() {
    return s_available;
}

// =============================================================================
//  LOG SENSOR ROW
// =============================================================================
void sd_log_sensor(const char* date_str, const char* time_str,
                   float tempC, float humidity, float pressureHPa,
                   float gasResistanceOhm, float iaqScore, uint8_t iaqAccuracy) {
    if (!s_available) return;

    // Build file path: /sensor/YYYY-MM-DD.csv
    char path[32];
    snprintf(path, sizeof(path), "/sensor/%s.csv", date_str);

    bool write_header = !SD.exists(path);

    // FILE_WRITE appends on ESP32 SD library when file already exists,
    // but to be explicit we open with FILE_APPEND when available.
    // The third arg (true) = create if not exists.
    File f = SD.open(path, FILE_APPEND, true);
    if (!f) {
        Serial.printf("[SD] Failed to open %s for append\n", path);
        return;
    }

    if (write_header) {
        f.println("timestamp,tempC,humidity,pressureHPa,gasResistanceOhm,iaqScore,iaqAccuracy");
    }

    // timestamp = date + T + time  e.g. 2026-04-05T14:35:00+11:00
    f.printf("%sT%s,%.2f,%.2f,%.2f,%.1f,%.1f,%u\n",
             date_str, time_str,
             tempC, humidity, pressureHPa,
             gasResistanceOhm, iaqScore, iaqAccuracy);

    f.close();
}

// =============================================================================
//  SAVE BASELINE
// =============================================================================
void sd_save_baseline(float iaq_baseline_ohm, float pressure_ref_hpa,
                      uint32_t cumulative_s) {
    if (!s_available) return;

    File f = SD.open("/config/baseline.json", FILE_WRITE, true);
    if (!f) {
        Serial.println("[SD] Failed to open /config/baseline.json for write");
        return;
    }

    // Write JSON manually — no DynamicJsonDocument allocation needed for 3 fields
    f.printf("{\n  \"iaq_baseline_ohm\": %.2f,\n  \"pressure_ref_hpa\": %.2f,\n  \"cumulative_s\": %lu\n}\n",
             iaq_baseline_ohm, pressure_ref_hpa, (unsigned long)cumulative_s);
    f.close();

    Serial.printf("[SD] Baseline saved: iaq=%.0f ohm, pres=%.2f hPa, runtime=%lu s\n",
                  iaq_baseline_ohm, pressure_ref_hpa, (unsigned long)cumulative_s);
}

// =============================================================================
//  LOAD BASELINE
// =============================================================================
SdBaseline sd_load_baseline() {
    SdBaseline result = {};

    if (!s_available) return result;
    if (!SD.exists("/config/baseline.json")) {
        Serial.println("[SD] No baseline.json found — will create on first save.");
        return result;
    }

    File f = SD.open("/config/baseline.json", FILE_READ);
    if (!f) return result;

    // Read entire file into a buffer
    size_t sz = f.size();
    if (sz == 0 || sz > 512) { f.close(); return result; }

    char buf[512];
    size_t n = f.readBytes(buf, sz);
    f.close();
    buf[n] = '\0';

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) {
        Serial.printf("[SD] baseline.json parse error: %s\n", err.c_str());
        return result;
    }

    result.iaq_baseline_ohm = doc["iaq_baseline_ohm"] | 0.0f;
    result.pressure_ref_hpa = doc["pressure_ref_hpa"] | 0.0f;
    result.cumulative_s     = doc["cumulative_s"]     | (uint32_t)0;
    result.valid            = (result.iaq_baseline_ohm > 1000.0f);

    Serial.printf("[SD] Baseline loaded: iaq=%.0f ohm, pres=%.2f hPa, runtime=%lu s\n",
                  result.iaq_baseline_ohm, result.pressure_ref_hpa,
                  (unsigned long)result.cumulative_s);
    return result;
}

// =============================================================================
//  LOAD HISTORY
// =============================================================================
// Parse one CSV data row into out_temp/hum/pres arrays at position `slot`.
// Returns true if row was valid data (not header, not malformed).
static bool parse_csv_row(const char* line,
                           float* out_temp, float* out_hum, float* out_pres,
                           int slot) {
    // Expected format:
    // 2026-04-05T14:35:00+11:00,23.40,61.20,1013.70,45230.5,87.3,2
    // Skip header line
    if (line[0] == 't' || line[0] == 'T') return false;  // "timestamp"

    // Find the 2nd comma (after timestamp field)
    const char* p = line;
    int commas = 0;
    while (*p && commas < 1) { if (*p == ',') commas++; p++; }
    if (!*p) return false;

    float temp, hum, pres;
    if (sscanf(p, "%f,%f,%f", &temp, &hum, &pres) != 3) return false;

    out_temp[slot] = temp;
    out_hum[slot]  = hum;
    out_pres[slot] = pres;
    return true;
}

// Read rows from one CSV file, filling slots starting at `start_slot`.
// Returns number of slots filled.
static int read_csv_file(const char* path,
                          float* out_temp, float* out_hum, float* out_pres,
                          int start_slot, int max_slots) {
    if (!SD.exists(path)) return 0;

    File f = SD.open(path, FILE_READ);
    if (!f) return 0;

    // Count total lines first so we can skip older rows if file has more than needed
    int total_lines = 0;
    while (f.available()) {
        char c = f.read();
        if (c == '\n') total_lines++;
    }
    // Subtract 1 for header, 1 for possible trailing blank line
    int data_lines = max(0, total_lines - 1);

    // How many rows from this file do we actually want?
    int slots_remaining = max_slots - start_slot;
    int skip = max(0, data_lines - slots_remaining);

    // Rewind
    f.seek(0);

    char line[128];
    int line_num = 0;
    int filled   = 0;

    while (f.available() && (start_slot + filled) < max_slots) {
        // Read one line
        int i = 0;
        while (f.available() && i < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[i++] = c;
        }
        line[i] = '\0';
        if (i == 0) continue;

        line_num++;
        if (line_num <= skip + 1) continue;  // +1 for header

        if (parse_csv_row(line, out_temp, out_hum, out_pres, start_slot + filled)) {
            filled++;
        }
    }

    f.close();
    return filled;
}

SdHistoryResult sd_load_history(float* out_temp, float* out_hum,
                                 float* out_pres, int max_slots,
                                 const char* today_date,
                                 const char* yesterday_date) {
    SdHistoryResult result = {};
    if (!s_available) return result;

    // Strategy: fill slots 0..N-1 in chronological order.
    // Try yesterday first (older data), then today (newer data).
    // If today alone has enough rows, skip yesterday.

    char path_today[32], path_yesterday[32];
    snprintf(path_today,     sizeof(path_today),     "/sensor/%s.csv", today_date);
    snprintf(path_yesterday, sizeof(path_yesterday),  "/sensor/%s.csv", yesterday_date);

    // Count today's data rows
    int today_rows = 0;
    if (SD.exists(path_today)) {
        File f = SD.open(path_today, FILE_READ);
        if (f) {
            while (f.available()) { if (f.read() == '\n') today_rows++; }
            f.close();
            today_rows = max(0, today_rows - 1);  // subtract header
        }
    }

    int filled = 0;

    if (today_rows < max_slots && SD.exists(path_yesterday)) {
        // Need some rows from yesterday to fill the window
        int need_from_yesterday = max_slots - today_rows;
        filled = read_csv_file(path_yesterday, out_temp, out_hum, out_pres,
                               0, need_from_yesterday);
    }

    // Now fill the rest from today
    int from_today = read_csv_file(path_today, out_temp, out_hum, out_pres,
                                   filled, max_slots);
    filled += from_today;

    result.slots_loaded = filled;
    result.valid        = (filled > 0);

    Serial.printf("[SD] History loaded: %d slots (%d from today, rest from yesterday)\n",
                  filled, from_today);
    return result;
}

// =============================================================================
//  EXTENDED HISTORY — on-demand read for 24h / 7d stats screen
// =============================================================================

// Parse timestamp field at start of CSV row into day-offset from date_today.
// Returns 0 = today, 1 = yesterday, 2 = 2 days ago, etc.
// Returns -1 if unparseable.
static int csv_day_offset(const char* line, const char* date_today) {
    // Line starts with: YYYY-MM-DDT...
    if (strlen(line) < 10) return -1;
    // date_today is "YYYY-MM-DD" — compare YYYY, MM, DD individually
    int ty, tm, td, ly, lm, ld;
    if (sscanf(date_today, "%d-%d-%d", &ty, &tm, &td) != 3) return -1;
    if (sscanf(line, "%d-%d-%d", &ly, &lm, &ld) != 3) return -1;
    // Simple ordinal offset (ignores DST edge cases — fine for 7-day sensor history)
    // Convert both to days since a fixed epoch using Julian Day Number
    auto jdn = [](int y, int m, int d) -> int {
        return (1461 * (y + 4800 + (m - 14) / 12)) / 4
             + (367 * (m - 2 - 12 * ((m - 14) / 12))) / 12
             - (3 * ((y + 4900 + (m - 14) / 12) / 100)) / 4 + d - 32075;
    };
    return jdn(ty, tm, td) - jdn(ly, lm, ld);
}

// Compute stats from a float array (ignores 0.0 as sentinel for missing)
static void compute_stats(const float* data, int n,
                           float &out_hi, float &out_lo, float &out_avg) {
    if (n == 0) { out_hi = out_lo = out_avg = 0.0f; return; }
    float mn = data[0], mx = data[0], sum = 0.0f;
    for (int i = 0; i < n; i++) {
        if (data[i] < mn) mn = data[i];
        if (data[i] > mx) mx = data[i];
        sum += data[i];
    }
    out_hi  = mx;
    out_lo  = mn;
    out_avg = sum / n;
}

SdExtResult sd_read_extended(float* out_temp, float* out_hum, float* out_pres,
                              int max_slots, int window_days,
                              const char* date_today) {
    SdExtResult result = {};
    if (!s_available || max_slots < 1 || window_days < 1) return result;

    // We'll read raw rows into large temporary arrays first, then downsample.
    // Max raw rows: window_days * 288 (24*12). Use heap for temp storage.
    const int MAX_RAW = window_days * 300;   // small over-alloc
    float* raw_temp  = (float*)malloc(MAX_RAW * sizeof(float));
    float* raw_hum   = (float*)malloc(MAX_RAW * sizeof(float));
    float* raw_pres  = (float*)malloc(MAX_RAW * sizeof(float));
    int*   raw_day   = (int*)  malloc(MAX_RAW * sizeof(int));   // day offset per row
    if (!raw_temp || !raw_hum || !raw_pres || !raw_day) {
        free(raw_temp); free(raw_hum); free(raw_pres); free(raw_day);
        Serial.println("[SD] sd_read_extended: malloc failed");
        return result;
    }

    int raw_count = 0;

    // Parse date_today into year/month/day so we can walk back day by day
    int ty, tm, td;
    if (sscanf(date_today, "%d-%d-%d", &ty, &tm, &td) != 3) {
        free(raw_temp); free(raw_hum); free(raw_pres); free(raw_day);
        return result;
    }

    // We read oldest days first (day window_days-1 ago) to newest (today=0)
    // Build date strings by walking backward then read files in reverse order.
    // Store into raw arrays in chronological order (oldest first).
    for (int day = window_days - 1; day >= 0; day--) {
        // Compute date for `day` days ago
        // Simple approach: start from today and subtract days
        struct tm t = {};
        t.tm_year = ty - 1900;
        t.tm_mon  = tm - 1;
        t.tm_mday = td - day;
        mktime(&t);  // normalises the struct (handles month/year rollover)
        char date_str[12];
        snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        char path[32];
        snprintf(path, sizeof(path), "/sensor/%s.csv", date_str);
        if (!SD.exists(path)) continue;

        File f = SD.open(path, FILE_READ);
        if (!f) continue;

        char line[128];
        while (f.available() && raw_count < MAX_RAW) {
            int i = 0;
            while (f.available() && i < (int)sizeof(line) - 1) {
                char c = f.read();
                if (c == '\n') break;
                if (c != '\r') line[i++] = c;
            }
            line[i] = '\0';
            if (i == 0) continue;
            if (line[0] == 't' || line[0] == 'T') continue;  // header

            float tmp, hum, pres;
            const char* p = line;
            int commas = 0;
            while (*p && commas < 1) { if (*p == ',') commas++; p++; }
            if (!*p) continue;
            if (sscanf(p, "%f,%f,%f", &tmp, &hum, &pres) != 3) continue;

            raw_temp[raw_count] = tmp;
            raw_hum[raw_count]  = hum;
            raw_pres[raw_count] = pres;
            raw_day[raw_count]  = day;   // 0=today, 1=yesterday, etc.
            raw_count++;
        }
        f.close();
    }

    Serial.printf("[SD] sd_read_extended: %d raw rows, window=%d days\n",
                  raw_count, window_days);

    if (raw_count == 0) {
        free(raw_temp); free(raw_hum); free(raw_pres); free(raw_day);
        return result;
    }

    // ── Compute per-day stats ─────────────────────────────────────────────────
    // today = day offset 0, yesterday = 1
    // Use temporary small arrays for per-day stats computation
    const int MAX_DAY_ROWS = 300;
    float* day_t = (float*)malloc(MAX_DAY_ROWS * sizeof(float));
    float* day_h = (float*)malloc(MAX_DAY_ROWS * sizeof(float));
    float* day_p = (float*)malloc(MAX_DAY_ROWS * sizeof(float));

    if (day_t && day_h && day_p) {
        // TODAY (day offset = 0)
        int n = 0;
        for (int i = 0; i < raw_count && n < MAX_DAY_ROWS; i++)
            if (raw_day[i] == 0) { day_t[n]=raw_temp[i]; day_h[n]=raw_hum[i]; day_p[n]=raw_pres[i]; n++; }
        compute_stats(day_t, n, result.temp_today_hi, result.temp_today_lo, result.temp_today_avg);
        compute_stats(day_h, n, result.hum_today_hi,  result.hum_today_lo,  result.hum_today_avg);
        compute_stats(day_p, n, result.pres_today_hi, result.pres_today_lo, result.pres_today_avg);

        // YESTERDAY (day offset = 1)
        n = 0;
        for (int i = 0; i < raw_count && n < MAX_DAY_ROWS; i++)
            if (raw_day[i] == 1) { day_t[n]=raw_temp[i]; day_h[n]=raw_hum[i]; day_p[n]=raw_pres[i]; n++; }
        compute_stats(day_t, n, result.temp_yest_hi, result.temp_yest_lo, result.temp_yest_avg);
        compute_stats(day_h, n, result.hum_yest_hi,  result.hum_yest_lo,  result.hum_yest_avg);
        compute_stats(day_p, n, result.pres_yest_hi, result.pres_yest_lo, result.pres_yest_avg);

        // 7-DAY (all rows in window)
        compute_stats(raw_temp, raw_count, result.temp_7d_hi, result.temp_7d_lo, result.temp_7d_avg);
        compute_stats(raw_hum,  raw_count, result.hum_7d_hi,  result.hum_7d_lo,  result.hum_7d_avg);
        compute_stats(raw_pres, raw_count, result.pres_7d_hi, result.pres_7d_lo, result.pres_7d_avg);

        // 7d vs prev 7d trend: compare avg of days 0-6 vs days 7-13
        // Only meaningful if window_days >= 14 — signal NaN otherwise
        if (window_days >= 14) {
            float cur_t=0, prev_t=0, cur_h=0, prev_h=0, cur_p=0, prev_p=0;
            int nc=0, np=0;
            for (int i=0; i<raw_count; i++) {
                if (raw_day[i] < 7)  { cur_t+=raw_temp[i]; cur_h+=raw_hum[i]; cur_p+=raw_pres[i]; nc++; }
                if (raw_day[i] >= 7) { prev_t+=raw_temp[i];prev_h+=raw_hum[i];prev_p+=raw_pres[i];np++; }
            }
            if (nc>0 && np>0) {
                result.temp_7d_vs_prev = (cur_t/nc) - (prev_t/np);
                result.hum_7d_vs_prev  = (cur_h/nc) - (prev_h/np);
                result.pres_7d_vs_prev = (cur_p/nc) - (prev_p/np);
            } else {
                result.temp_7d_vs_prev = result.hum_7d_vs_prev = result.pres_7d_vs_prev = NAN;
            }
        } else {
            result.temp_7d_vs_prev = result.hum_7d_vs_prev = result.pres_7d_vs_prev = NAN;
        }
    }
    free(day_t); free(day_h); free(day_p);

    // ── Downsample raw rows into caller arrays ────────────────────────────────
    if (raw_count <= max_slots) {
        // No downsampling needed — copy directly
        memcpy(out_temp, raw_temp, raw_count * sizeof(float));
        memcpy(out_hum,  raw_hum,  raw_count * sizeof(float));
        memcpy(out_pres, raw_pres, raw_count * sizeof(float));
        result.slots_loaded = raw_count;
    } else {
        // Average-downsample: group raw rows into max_slots buckets
        float bucket_sz = (float)raw_count / max_slots;
        for (int s = 0; s < max_slots; s++) {
            int start = (int)(s * bucket_sz);
            int end   = (int)((s + 1) * bucket_sz);
            if (end > raw_count) end = raw_count;
            float st=0, sh=0, sp=0;
            int cnt = end - start;
            for (int i=start; i<end; i++) { st+=raw_temp[i]; sh+=raw_hum[i]; sp+=raw_pres[i]; }
            out_temp[s] = (cnt > 0) ? st/cnt : 0.0f;
            out_hum[s]  = (cnt > 0) ? sh/cnt : 0.0f;
            out_pres[s] = (cnt > 0) ? sp/cnt : 0.0f;
        }
        result.slots_loaded = max_slots;
    }

    result.valid = (result.slots_loaded > 0);
    free(raw_temp); free(raw_hum); free(raw_pres); free(raw_day);

    Serial.printf("[SD] sd_read_extended: %d output slots, today hi/lo: %.1f/%.1f\n",
                  result.slots_loaded, result.temp_today_hi, result.temp_today_lo);
    return result;
}
