// =============================================================================
//  earthquake_api.cpp  —  USGS FDSNWS earthquake query for Tab5 Weather Dashboard
//
//  Uses the USGS FDSNWS Event Query API with geographic bounding box parameters
//  so only events in our region are returned — tiny targeted response (~10-40KB).
//  No client-side region filtering needed. No streaming/chunked encoding issues.
//
//  API docs: https://earthquake.usgs.gov/fdsnws/event/1/
// =============================================================================

#include "earthquake_api.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

#define EQ_TIMEOUT_MS  15000

// FDSNWS query URL — bounding box pre-applied, last 7 days, M2.5+, max 100 events
static const char EQ_URL[] =
    "https://earthquake.usgs.gov/fdsnws/event/1/query"
    "?format=geojson"
    "&minlatitude=-55"
    "&maxlatitude=22"
    "&minlongitude=108"
    "&maxlongitude=182"
    "&minmagnitude=2.5"
    "&orderby=time"
    "&limit=100";

// =============================================================================
//  HELPERS
// =============================================================================

const char* eq_depth_label(float depth_km) {
    if (depth_km < 70.0f)  return "Shallow";
    if (depth_km < 300.0f) return "Intermediate";
    return "Deep";
}

int eq_dist_from_sydney(float lat, float lon) {
    const float R = 6371.0f;
    float dlat = (lat - SYDNEY_LAT) * (float)M_PI / 180.0f;
    float dlon = (lon - SYDNEY_LON) * (float)M_PI / 180.0f;
    float lat1 = SYDNEY_LAT * (float)M_PI / 180.0f;
    float lat2 = lat        * (float)M_PI / 180.0f;
    float a = sinf(dlat/2)*sinf(dlat/2) +
              cosf(lat1)*cosf(lat2)*sinf(dlon/2)*sinf(dlon/2);
    float c = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f-a));
    return (int)(R * c);
}

// =============================================================================
//  FETCH
// =============================================================================

EqResult eq_fetch() {
    EqResult result = {};
    result.valid       = false;
    result.largest_idx = 0;

    Serial.println("[EQ] Fetching (FDSNWS region query)...");

    HTTPClient http;
    http.setTimeout(EQ_TIMEOUT_MS);
    http.setUserAgent("Tab5WeatherDash/1.0");
    http.begin(EQ_URL);

    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[EQ] HTTP error: %d\n", httpCode);
        http.end();
        return result;
    }

    int contentLen = http.getSize();
    Serial.printf("[EQ] HTTP 200, content-length=%d bytes\n", contentLen);

    // content-length=-1 means chunked transfer encoding.
    // getStreamPtr() doesn't work reliably with chunked responses on ESP32.
    // Use getString() which handles chunked assembly automatically.
    String body = http.getString();
    http.end();

    if (body.length() == 0) {
        Serial.println("[EQ] Empty response body");
        return result;
    }

    Serial.printf("[EQ] Body length: %d bytes\n", (int)body.length());

    // Use a filter to extract only the 3 fields we need per feature.
    // This keeps the ArduinoJson doc tiny regardless of response size.
    // Without filter: 71KB JSON needs ~100KB doc. With filter: ~8KB doc.
    StaticJsonDocument<128> filter;
    filter["features"][0]["properties"]["mag"]   = true;
    filter["features"][0]["properties"]["place"] = true;
    filter["features"][0]["properties"]["time"]  = true;
    filter["features"][0]["geometry"]["coordinates"] = true;

    // Filtered: 100 events × ~80 bytes each = 8KB; 16KB for safety.
    DynamicJsonDocument doc(16384);

    Serial.println("[EQ] Parsing...");
    DeserializationError err = deserializeJson(doc, body,
                                DeserializationOption::Filter(filter));

    if (err) {
        Serial.printf("[EQ] Parse error: %s  doc_usage=%d\n",
                      err.c_str(), (int)doc.memoryUsage());
        return result;
    }

    JsonArray features = doc["features"];
    if (features.isNull()) {
        Serial.printf("[EQ] No features. doc_usage=%d\n", (int)doc.memoryUsage());
        return result;
    }

    Serial.printf("[EQ] Features: %d  doc_usage=%d bytes\n",
                  (int)features.size(), (int)doc.memoryUsage());

    time_t now_utc = time(nullptr);
    result.total_global = features.size();
    float largest_mag = -99.0f;

    for (JsonObject feat : features) {
        float mag = feat["properties"]["mag"] | -99.0f;
        if (mag < 2.5f) continue;

        JsonArray coords = feat["geometry"]["coordinates"];
        if (coords.isNull() || coords.size() < 3) continue;

        float lon    = coords[0];
        float lat    = coords[1];
        float depth  = coords[2];
        int64_t t_ms = feat["properties"]["time"] | (int64_t)0;

        if (result.count >= EQ_MAX) break;

        EqEvent &ev  = result.events[result.count];
        ev.mag       = mag;
        ev.lat       = lat;
        ev.lon       = lon;
        ev.depth_km  = depth;
        ev.dist_km   = eq_dist_from_sydney(lat, lon);

        if (t_ms > 0 && now_utc > 0) {
            int64_t age_s  = (int64_t)now_utc - (t_ms / 1000LL);
            ev.age_minutes = (int32_t)(age_s / 60);
        } else {
            ev.age_minutes = -1;
        }

        const char *place = feat["properties"]["place"] | "";
        strncpy(ev.place, place, EQ_PLACE_LEN - 1);
        ev.place[EQ_PLACE_LEN - 1] = '\0';

        if (mag >= 6.0f) result.count_m6plus++;
        if (mag >= 5.0f) result.count_m5plus++;
        if (mag >= 4.0f) result.count_m4plus++;

        if (mag > largest_mag) {
            largest_mag        = mag;
            result.largest_idx = result.count;
        }

        result.count++;
    }

    struct tm *lt = localtime(&now_utc);
    snprintf(result.fetch_time, sizeof(result.fetch_time),
             "%02d:%02d %02d/%02d",
             lt->tm_hour, lt->tm_min, lt->tm_mday, lt->tm_mon + 1);

    result.valid = true;
    Serial.printf("[EQ] OK: %d events, largest M%.1f\n",
                  result.count, largest_mag > -99 ? largest_mag : 0.0f);
    return result;
}
