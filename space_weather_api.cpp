// =============================================================================
//  space_weather_api.cpp  —  BOM Space Weather Services API
//  https://sws-data.sws.bom.gov.au/api/v1/
//
//  All endpoints use HTTP POST with JSON body {"api_key":"...","options":{...}}
//  All timestamps in responses are UTC.
//  JSON doc sizes are kept small — each endpoint returns tiny payloads.
// =============================================================================

#include "space_weather_api.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Base URL prefix
static const char *SW_BASE = "https://sws-data.sws.bom.gov.au/api/v1/";

// ── Helper: POST JSON to a BOM SWS endpoint, return response String ────────────
// Returns empty String on failure.
static String sw_post(const char *endpoint, const String &body) {
    char url[128];
    snprintf(url, sizeof(url), "%s%s", SW_BASE, endpoint);

    WiFiClientSecure client;
    client.setInsecure();   // hobby use — same pattern as weather_api.cpp

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json; charset=UTF-8");
    http.setTimeout(8000);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("[SpaceWx] %s → HTTP %d\n", endpoint, code);
        http.end();
        return String();
    }
    String resp = http.getString();
    http.end();
    return resp;
}

// ── Helper: build minimal POST body with just API key ─────────────────────────
static String sw_body_simple() {
    // {"api_key":"YOUR_KEY"}
    String b = "{\"api_key\":\"";
    b += BOM_SW_API_KEY;
    b += "\"}";
    return b;
}

// ── Helper: build POST body with options ──────────────────────────────────────
static String sw_body_opts(const char *optsJson) {
    // {"api_key":"KEY","options":{...}}
    String b = "{\"api_key\":\"";
    b += BOM_SW_API_KEY;
    b += "\",\"options\":";
    b += optsJson;
    b += "}";
    return b;
}

// ── Helper: safe strncpy (always null-terminates) ─────────────────────────────
static void sw_strcpy(char *dst, const char *src, size_t n) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

// =============================================================================
//  HUMAN-READABLE LABEL HELPERS
// =============================================================================

const char* space_k_label(int k) {
    if (k <= 2) return "Quiet";
    if (k == 3) return "Unsettled";
    if (k == 4) return "Active";
    if (k == 5) return "Storm G1";
    if (k == 6) return "Storm G2";
    if (k == 7) return "Storm G3";
    if (k == 8) return "Storm G4";
    return "Storm G5";
}

const char* space_a_label(int a) {
    if (a < 8)  return "Quiet";
    if (a < 16) return "Unsettled";
    if (a < 30) return "Active";
    if (a < 50) return "Minor storm";
    return "Major storm";
}

const char* space_dst_label(int dst) {
    if (dst > -20) return "Quiet";
    if (dst > -50) return "Moderate storm";
    if (dst > -100) return "Intense storm";
    return "Severe storm";
}

const char* space_alert_label(SpaceAlertLevel lvl) {
    switch (lvl) {
        case SPACE_ALERT:   return "ALERT";
        case SPACE_WATCH:   return "WATCH";
        case SPACE_OUTLOOK: return "OUTLOOK";
        default:            return "QUIET";
    }
}

// =============================================================================
//  MAIN FETCH
// =============================================================================

SpaceWeatherResult space_weather_fetch() {
    SpaceWeatherResult r = {};
    r.valid = false;
    // Initialise K history to -1 (no data)
    for (int i = 0; i < KINDEX_HISTORY; i++) r.kHistory[i] = -1;
    r.kIndex   = -1;
    r.aIndex   = -1;
    r.dstIndex = 0;

    Serial.println("[SpaceWx] Fetching all endpoints…");

    // ── 1. K-index (latest) ───────────────────────────────────────────────────
    {
        String body = sw_body_opts("{\"location\":\"Australian region\"}");
        String resp = sw_post("get-k-index", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(512);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                if (arr.size() > 0) {
                    auto latest = arr[arr.size() - 1];
                    r.kIndex = latest["index"].as<int>();
                    sw_strcpy(r.kTime, latest["valid_time"].as<const char*>(),
                              sizeof(r.kTime));
                    Serial.printf("[SpaceWx] K-index: %d at %s\n", r.kIndex, r.kTime);
                }
            }
        }
    }

    // ── 2. K-index history (last 24 hours) ────────────────────────────────────
    // Compute UTC start = now - 24h, end = now
    {
        time_t now_t = time(nullptr);
        time_t start_t = now_t - 24 * 3600;
        // Copy into local structs immediately — gmtime() returns a pointer to a
        // single shared static buffer, so the second call overwrites the first.
        struct tm ts_buf, te_buf;
        { struct tm *tmp = gmtime(&start_t); ts_buf = *tmp; }
        { struct tm *tmp = gmtime(&now_t);   te_buf = *tmp; }
        char start_str[24], end_str[24];
        snprintf(start_str, sizeof(start_str), "%04d-%02d-%02d %02d:%02d:%02d",
                 ts_buf.tm_year+1900, ts_buf.tm_mon+1, ts_buf.tm_mday,
                 ts_buf.tm_hour, ts_buf.tm_min, ts_buf.tm_sec);
        snprintf(end_str, sizeof(end_str), "%04d-%02d-%02d %02d:%02d:%02d",
                 te_buf.tm_year+1900, te_buf.tm_mon+1, te_buf.tm_mday,
                 te_buf.tm_hour, te_buf.tm_min, te_buf.tm_sec);

        char opts[128];
        snprintf(opts, sizeof(opts),
                 "{\"location\":\"Australian region\","
                 "\"start\":\"%s\",\"end\":\"%s\"}",
                 start_str, end_str);
        String body = sw_body_opts(opts);
        String resp = sw_post("get-k-index", body);
        if (resp.length() > 0) {
            // Larger doc for up to 8 readings × small objects
            DynamicJsonDocument doc(2048);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                int n = arr.size();
                // Take the last KINDEX_HISTORY readings (most recent)
                int start_i = (n > KINDEX_HISTORY) ? n - KINDEX_HISTORY : 0;
                int slot = 0;
                for (int i = start_i; i < n && slot < KINDEX_HISTORY; i++, slot++) {
                    r.kHistory[slot] = arr[i]["index"].as<int>();
                    sw_strcpy(r.kHistoryTime[slot],
                              arr[i]["valid_time"].as<const char*>(),
                              sizeof(r.kHistoryTime[slot]));
                }
                Serial.printf("[SpaceWx] K-index history: %d readings\n", slot);
            }
        }
    }

    // ── 3. A-index (latest) ───────────────────────────────────────────────────
    {
        String body = sw_body_opts("{\"location\":\"Australian region\"}");
        String resp = sw_post("get-a-index", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(512);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                if (arr.size() > 0) {
                    auto latest = arr[arr.size() - 1];
                    r.aIndex = latest["index"].as<int>();
                    sw_strcpy(r.aTime, latest["valid_time"].as<const char*>(),
                              sizeof(r.aTime));
                    Serial.printf("[SpaceWx] A-index: %d\n", r.aIndex);
                }
            }
        }
    }

    // ── 4. Dst index (latest) ─────────────────────────────────────────────────
    {
        String body = sw_body_opts("{\"location\":\"Australian region\"}");
        String resp = sw_post("get-dst-index", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(512);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                if (arr.size() > 0) {
                    auto latest = arr[arr.size() - 1];
                    r.dstIndex = latest["index"].as<int>();
                    sw_strcpy(r.dstTime, latest["valid_time"].as<const char*>(),
                              sizeof(r.dstTime));
                    Serial.printf("[SpaceWx] Dst: %d nT\n", r.dstIndex);
                }
            }
        }
    }

    // ── 5. Magnetic alert ─────────────────────────────────────────────────────
    {
        String body = sw_body_simple();
        String resp = sw_post("get-mag-alert", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(1024);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                r.magAlert = (arr.size() > 0);
                if (r.magAlert) {
                    sw_strcpy(r.magAlertDesc,
                              arr[0]["description"].as<const char*>(),
                              sizeof(r.magAlertDesc));
                    Serial.println("[SpaceWx] Magnetic alert ACTIVE");
                }
            }
        }
    }

    // ── 6. Magnetic / geophysical warning ─────────────────────────────────────
    {
        String body = sw_body_simple();
        String resp = sw_post("get-mag-warning", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(1024);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                r.magWarning = (arr.size() > 0);
                if (r.magWarning) {
                    sw_strcpy(r.magWarningDesc,
                              arr[0]["description"].as<const char*>(),
                              sizeof(r.magWarningDesc));
                    Serial.println("[SpaceWx] Magnetic warning ACTIVE");
                }
            }
        }
    }

    // ── 7. Aurora alert (in-progress) ─────────────────────────────────────────
    {
        String body = sw_body_simple();
        String resp = sw_post("get-aurora-alert", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(1024);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                r.auroraAlert = (arr.size() > 0);
                if (r.auroraAlert) {
                    auto a = arr[0];
                    r.auroraAlertK = a["k_aus"].as<int>();
                    sw_strcpy(r.auroraAlertBand, a["lat_band"].as<const char*>(),
                              sizeof(r.auroraAlertBand));
                    sw_strcpy(r.auroraAlertDesc, a["description"].as<const char*>(),
                              sizeof(r.auroraAlertDesc));
                    sw_strcpy(r.auroraAlertUntil, a["valid_until"].as<const char*>(),
                              sizeof(r.auroraAlertUntil));
                    Serial.printf("[SpaceWx] Aurora ALERT: K=%d band=%s\n",
                                  r.auroraAlertK, r.auroraAlertBand);
                }
            }
        }
    }

    // ── 8. Aurora watch (48h) ─────────────────────────────────────────────────
    {
        String body = sw_body_simple();
        String resp = sw_post("get-aurora-watch", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(1024);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                r.auroraWatch = (arr.size() > 0);
                if (r.auroraWatch) {
                    auto a = arr[0];
                    r.auroraWatchK = a["k_aus"].as<int>();
                    sw_strcpy(r.auroraWatchBand, a["lat_band"].as<const char*>(),
                              sizeof(r.auroraWatchBand));
                    sw_strcpy(r.auroraWatchDesc, a["description"].as<const char*>(),
                              sizeof(r.auroraWatchDesc));
                    sw_strcpy(r.auroraWatchUntil, a["valid_until"].as<const char*>(),
                              sizeof(r.auroraWatchUntil));
                    Serial.printf("[SpaceWx] Aurora WATCH: K=%d band=%s\n",
                                  r.auroraWatchK, r.auroraWatchBand);
                }
            }
        }
    }

    // ── 9. Aurora outlook (3-7 day) ───────────────────────────────────────────
    {
        String body = sw_body_simple();
        String resp = sw_post("get-aurora-outlook", body);
        if (resp.length() > 0) {
            DynamicJsonDocument doc(1024);
            if (!deserializeJson(doc, resp)) {
                auto arr = doc["data"];
                r.auroraOutlook = (arr.size() > 0);
                if (r.auroraOutlook) {
                    auto a = arr[0];
                    r.auroraOutlookK = a["k_aus"].as<int>();
                    sw_strcpy(r.auroraOutlookBand, a["lat_band"].as<const char*>(),
                              sizeof(r.auroraOutlookBand));
                    sw_strcpy(r.auroraOutlookStart, a["start_date"].as<const char*>(),
                              sizeof(r.auroraOutlookStart));
                    sw_strcpy(r.auroraOutlookEnd, a["end_date"].as<const char*>(),
                              sizeof(r.auroraOutlookEnd));
                    sw_strcpy(r.auroraOutlookCause, a["cause"].as<const char*>(),
                              sizeof(r.auroraOutlookCause));
                    Serial.printf("[SpaceWx] Aurora OUTLOOK: K=%d band=%s (%s–%s)\n",
                                  r.auroraOutlookK, r.auroraOutlookBand,
                                  r.auroraOutlookStart, r.auroraOutlookEnd);
                }
            }
        }
    }

    // ── Derive highest alert level ────────────────────────────────────────────
    r.alertLevel = SPACE_QUIET;
    if (r.auroraOutlook) r.alertLevel = SPACE_OUTLOOK;
    if (r.auroraWatch)   r.alertLevel = SPACE_WATCH;
    if (r.auroraAlert)   r.alertLevel = SPACE_ALERT;

    r.valid     = true;
    r.fetchedAt = millis();
    Serial.printf("[SpaceWx] Fetch complete. Alert level: %s\n",
                  space_alert_label(r.alertLevel));
    return r;
}
