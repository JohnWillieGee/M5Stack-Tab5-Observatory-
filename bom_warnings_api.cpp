// =============================================================================
//  bom_warnings_api.cpp  —  BOM unofficial weather warnings API
//  https://api.weather.bom.gov.au/v1/locations/{geohash}/warnings
//
//  Two-call pattern per fetch cycle:
//    Call 1: GET /warnings           → list of {id, short_title, warning_group_type}
//    Call 2: GET /warnings/{id}      → detail: {title, areas, issue_time, expiry_time,
//                                               warning_group_type, text}
//    Calls 2 are repeated for each warning, up to BOM_WARN_MAX.
//    Total fetch time: ~1-3 s on good WiFi (each HTTPS call ~400-600 ms).
//
//  On any failure (HTTP != 200, parse error, timeout) valid=false is returned
//  and the caller falls back to WeatherAPI alerts already in WeatherResult.
// =============================================================================

#include "bom_warnings_api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static const char *BOM_API_BASE = "https://api.weather.bom.gov.au/v1";

// ── Helper: safe strncpy ──────────────────────────────────────────────────────
static void bw_strcpy(char *dst, const char *src, size_t n) {
    if (!src || !src[0]) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

// ── Helper: HTTP GET, returns response body or empty String on error ──────────
static String bom_get(const char *path) {
    char url[160];
    snprintf(url, sizeof(url), "%s%s", BOM_API_BASE, path);

    WiFiClientSecure client;
    client.setInsecure();   // BOM cert chain changes; hobby use only

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("User-Agent", "Mozilla/5.0");   // BOM app-style UA
    http.setTimeout(8000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[BomWarn] GET %s -> HTTP %d\n", path, code);
        http.end();
        return String();
    }
    String resp = http.getString();
    http.end();
    return resp;
}

// =============================================================================
//  bom_warn_severity — map BOM warning_group_type to severity label
// =============================================================================
const char* bom_warn_severity(const char *type) {
    if (!type || !type[0]) return "Unknown";
    // BOM type strings observed in the wild:
    if (strstr(type, "severe_thunderstorm"))  return "Severe";
    if (strstr(type, "tornado"))              return "Extreme";
    if (strstr(type, "tropical_cyclone"))     return "Extreme";
    if (strstr(type, "damaging_wind"))        return "Severe";
    if (strstr(type, "extreme_fire"))         return "Extreme";
    if (strstr(type, "fire"))                 return "Severe";
    if (strstr(type, "flood_watch"))          return "Minor";
    if (strstr(type, "flood_warning"))        return "Moderate";
    if (strstr(type, "major_flood"))          return "Severe";
    if (strstr(type, "sheep_graziers"))       return "Minor";
    if (strstr(type, "road"))                 return "Minor";
    if (strstr(type, "marine"))               return "Moderate";
    if (strstr(type, "hazardous_surf"))       return "Moderate";
    if (strstr(type, "tsunami"))              return "Extreme";
    if (strstr(type, "earthquake"))           return "Extreme";
    if (strstr(type, "landslip"))             return "Moderate";
    if (strstr(type, "heat"))                 return "Moderate";
    if (strstr(type, "frost"))                return "Minor";
    return "Minor";   // safe default
}

// =============================================================================
//  bom_warn_colour — severity → palette hex (matches alert_severity_colour())
// =============================================================================
uint32_t bom_warn_colour(const char *severity) {
    if (!severity) return 0x8B949E;
    if (strstr(severity, "Extreme"))  return 0xF85149;  // C_RED
    if (strstr(severity, "Severe"))   return 0xFF7B50;  // C_WARM orange
    if (strstr(severity, "Moderate")) return 0xD29922;  // C_YELLOW
    if (strstr(severity, "Minor"))    return 0x3FB950;  // C_GREEN
    return 0x8B949E;                                    // C_DIM unknown
}

// =============================================================================
//  bom_warnings_fetch — main entry point
// =============================================================================
BomWarningsResult bom_warnings_fetch() {
    BomWarningsResult r = {};
    r.valid        = false;
    r.usedFallback = false;
    r.count        = 0;

    Serial.println("[BomWarn] Fetching warnings list...");

    // ── Call 1: get warning list for Sydney geohash ───────────────────────────
    char list_path[64];
    snprintf(list_path, sizeof(list_path),
             "/locations/%s/warnings", BOM_SYDNEY_GEOHASH);

    String list_resp = bom_get(list_path);
    if (list_resp.length() == 0) {
        Serial.println("[BomWarn] List fetch failed — will use fallback");
        r.usedFallback = true;
        return r;
    }

    // Parse list — expect {"data":[{"id":"...","short_title":"...","warning_group_type":"..."},...]}
    DynamicJsonDocument list_doc(4096);
    DeserializationError err = deserializeJson(list_doc, list_resp);
    if (err) {
        Serial.printf("[BomWarn] List JSON parse error: %s\n", err.c_str());
        r.usedFallback = true;
        return r;
    }

    JsonArray list_arr = list_doc["data"].as<JsonArray>();
    if (list_arr.isNull()) {
        // Unexpected structure — treat as fetch success but no warnings
        Serial.println("[BomWarn] No 'data' array in list response");
        r.valid = true;
        return r;
    }

    int total = list_arr.size();
    Serial.printf("[BomWarn] List: %d warning(s) found\n", total);

    if (total == 0) {
        // Confirmed no active warnings for this location
        r.valid = true;
        return r;
    }

    // Fetch detail for each warning up to BOM_WARN_MAX
    int fetched = 0;
    for (int i = 0; i < total && fetched < BOM_WARN_MAX; i++) {
        JsonObject item = list_arr[i];
        const char *id   = item["id"]   | "";
        const char *type = item["warning_group_type"] | "";
        const char *stitle = item["short_title"] | "";

        if (!id[0]) continue;

        // ── Call 2: get warning detail ────────────────────────────────────────
        char detail_path[100];
        snprintf(detail_path, sizeof(detail_path), "/warnings/%s", id);

        String detail_resp = bom_get(detail_path);
        if (detail_resp.length() == 0) {
            Serial.printf("[BomWarn] Detail fetch failed for %s — skipping\n", id);
            // Still record it using list data, but no description
            BomWarning &w = r.warnings[fetched];
            bw_strcpy(w.id,    id,    sizeof(w.id));
            bw_strcpy(w.title, stitle[0] ? stitle : id, sizeof(w.title));
            bw_strcpy(w.type,  type,  sizeof(w.type));
            bw_strcpy(w.severity, bom_warn_severity(type), sizeof(w.severity));
            fetched++;
            continue;
        }

        DynamicJsonDocument det_doc(4096);
        DeserializationError det_err = deserializeJson(det_doc, detail_resp);
        if (det_err) {
            Serial.printf("[BomWarn] Detail parse error for %s: %s\n",
                          id, det_err.c_str());
            fetched++;
            continue;
        }

        JsonObject d = det_doc["data"];
        BomWarning &w = r.warnings[fetched];

        // ID and type from list (detail may not repeat them)
        bw_strcpy(w.id,   id,   sizeof(w.id));
        bw_strcpy(w.type, type, sizeof(w.type));

        // Title — prefer detail title, fall back to short_title from list
        const char *det_title = d["title"] | "";
        bw_strcpy(w.title,
                  det_title[0] ? det_title : stitle,
                  sizeof(w.title));

        // Areas — BOM puts this in "location" field as plain string
        const char *loc = d["location"] | "";
        bw_strcpy(w.areas, loc, sizeof(w.areas));

        // Timestamps
        bw_strcpy(w.issued,  d["issue_time"]   | "", sizeof(w.issued));
        bw_strcpy(w.expires, d["expiry_time"]  | "", sizeof(w.expires));

        // Description — BOM uses "text" field (may be long)
        const char *txt = d["text"] | "";
        bw_strcpy(w.desc, txt, sizeof(w.desc));   // truncates to BOM_WARN_DESC_LEN

        // Derive severity from type
        bw_strcpy(w.severity, bom_warn_severity(w.type), sizeof(w.severity));

        Serial.printf("[BomWarn] [%d] %s (%s)\n",
                      fetched, w.title, w.severity);
        fetched++;
    }

    r.count = fetched;
    r.valid = true;
    return r;
}
