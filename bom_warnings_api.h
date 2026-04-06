#pragma once
// =============================================================================
//  bom_warnings_api.h  —  BOM unofficial weather warnings API
//  Endpoint: https://api.weather.bom.gov.au/v1/locations/{geohash}/warnings
//  No API key required. Reverse-engineered from BOM's own website/app.
//  For personal/non-commercial use only (BOM copyright notice).
//
//  Fetch strategy:
//    1. GET /warnings  — returns list of active warning IDs + short titles
//    2. For each warning (up to BOM_WARN_MAX), GET /warnings/{id} — full detail
//    3. On any HTTP error / empty response, caller falls back to WeatherAPI alerts
// =============================================================================

#include <Arduino.h>

#define BOM_WARN_MAX       5    // max warnings to fetch and display
#define BOM_WARN_TITLE_LEN 80
#define BOM_WARN_AREA_LEN  96
#define BOM_WARN_DESC_LEN  220
#define BOM_WARN_ID_LEN    64
#define BOM_WARN_TYPE_LEN  48
#define BOM_WARN_TIME_LEN  24

// Sydney 6-character geohash (BOM API requires exactly 6 chars)
#define BOM_SYDNEY_GEOHASH  "r3gx2f"

struct BomWarning {
    char id[BOM_WARN_ID_LEN];          // e.g. "NSW_RC022_IDN36310"
    char title[BOM_WARN_TITLE_LEN];    // e.g. "Severe Thunderstorm Warning"
    char type[BOM_WARN_TYPE_LEN];      // e.g. "severe_thunderstorm"
    char areas[BOM_WARN_AREA_LEN];     // affected areas string
    char issued[BOM_WARN_TIME_LEN];    // ISO timestamp
    char expires[BOM_WARN_TIME_LEN];   // ISO timestamp
    char desc[BOM_WARN_DESC_LEN];      // warning description (truncated)
    // Severity derived from warning type — not provided directly by BOM
    // Mapped: extreme_* → Extreme, severe_* → Severe, flood_* → Moderate, else Minor
    char severity[20];
};

struct BomWarningsResult {
    BomWarning warnings[BOM_WARN_MAX];
    int        count;       // 0 = no active warnings
    bool       valid;       // true = fetch succeeded (even if count==0)
    bool       usedFallback; // true = BOM failed, caller should use WeatherAPI
};

// ── Public API ────────────────────────────────────────────────────────────────
BomWarningsResult bom_warnings_fetch();

// Map BOM warning type string to severity label
const char* bom_warn_severity(const char *type);

// Map severity string to display colour (same palette as alert_severity_colour)
// Returns hex colour value
uint32_t bom_warn_colour(const char *severity);
