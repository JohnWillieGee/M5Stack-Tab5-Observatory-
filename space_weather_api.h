#pragma once
// =============================================================================
//  space_weather_api.h  —  BOM Space Weather Services API
//  https://sws-data.sws.bom.gov.au/api/v1/
//  All endpoints use HTTP POST with JSON body.
//  All times in API responses are UTC.
// =============================================================================

#include <Arduino.h>

// ── Aurora / magnetic notice levels (ordered by severity) ─────────────────────
enum SpaceAlertLevel {
    SPACE_QUIET   = 0,   // no active notices
    SPACE_OUTLOOK = 1,   // 3-7 day aurora outlook
    SPACE_WATCH   = 2,   // 48h aurora watch
    SPACE_ALERT   = 3,   // aurora in progress
};

// ── K-index history — last 8 readings (covers ~24h at 3h cadence) ────────────
#define KINDEX_HISTORY  8

struct SpaceWeatherResult {
    // ── K-index (Australian region, ~5-min update) ────────────────────────────
    int   kIndex;                        // 0-9, current
    char  kTime[20];                     // UTC timestamp of reading
    int   kHistory[KINDEX_HISTORY];      // oldest→newest, -1 = no data
    char  kHistoryTime[KINDEX_HISTORY][20]; // UTC timestamp per slot

    // ── A-index (daily geomagnetic average) ───────────────────────────────────
    int   aIndex;                        // 0-400
    char  aTime[20];                     // UTC date of current value

    // ── Dst index (disturbance storm time, nT) ────────────────────────────────
    int   dstIndex;                      // nT, typically 0 to -300
    char  dstTime[20];

    // ── Magnetic alert ────────────────────────────────────────────────────────
    bool  magAlert;
    char  magAlertDesc[128];

    // ── Magnetic / geophysical warning ────────────────────────────────────────
    bool  magWarning;
    char  magWarningDesc[128];

    // ── Aurora notices (highest active level wins for display) ────────────────
    SpaceAlertLevel alertLevel;          // highest current level

    // Aurora alert (in-progress)
    bool  auroraAlert;
    int   auroraAlertK;                  // k_aus at time of alert
    char  auroraAlertBand[16];           // "high" / "mid" / "low" / "equatorial"
    char  auroraAlertDesc[256];
    char  auroraAlertUntil[20];

    // Aurora watch (48h warning)
    bool  auroraWatch;
    int   auroraWatchK;
    char  auroraWatchBand[16];
    char  auroraWatchDesc[256];
    char  auroraWatchUntil[20];

    // Aurora outlook (3-7 day)
    bool  auroraOutlook;
    int   auroraOutlookK;
    char  auroraOutlookBand[16];
    char  auroraOutlookStart[12];        // "2025-03-17" date only
    char  auroraOutlookEnd[12];
    char  auroraOutlookCause[64];        // "coronal mass ejection" etc.

    bool          valid;
    unsigned long fetchedAt;             // millis()
};

// ── Public API ────────────────────────────────────────────────────────────────
SpaceWeatherResult space_weather_fetch();

// Human-readable helpers
const char* space_k_label(int k);       // "Quiet" / "Unsettled" / "Storm G1" …
const char* space_a_label(int a);       // "Quiet" / "Unsettled" / "Active" …
const char* space_dst_label(int dst);   // "Quiet" / "Moderate" / "Intense" …
const char* space_alert_label(SpaceAlertLevel lvl); // "QUIET" / "OUTLOOK" …
