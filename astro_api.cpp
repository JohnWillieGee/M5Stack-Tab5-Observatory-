// =============================================================================
//  astro_api.cpp  —  AstronomyAPI.com planet positions
// =============================================================================

#include "astro_api.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Body IDs to fetch (in display order)
static const char *PLANET_IDS[ASTRO_PLANET_COUNT] = {
    "mercury", "venus", "mars", "jupiter", "saturn", "uranus", "neptune"
};
static const char *PLANET_NAMES[ASTRO_PLANET_COUNT] = {
    "Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune"
};

AstroResult astro_fetch(const char *date_str, const char *time_str) {
    AstroResult result = {};
    result.valid = false;

    if (!WiFi.isConnected()) {
        Serial.println("[AstroAPI] WiFi not connected");
        return result;
    }

    // Build URL — query all bodies in one request
    // GET /api/v2/bodies/positions?latitude=...&longitude=...&elevation=...
    //     &from_date=YYYY-MM-DD&to_date=YYYY-MM-DD&time=HH:MM:SS
    char url[320];
    snprintf(url, sizeof(url),
        "https://api.astronomyapi.com/api/v2/bodies/positions"
        "?latitude=%s&longitude=%s&elevation=%s"
        "&from_date=%s&to_date=%s&time=%s",
        ASTRO_LAT, ASTRO_LON, ASTRO_ELEVATION,
        date_str, date_str, time_str);

    Serial.printf("[AstroAPI] Fetching: %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", "Basic " ASTRO_API_AUTH);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[AstroAPI] HTTP %d\n", code);
        http.end();
        return result;
    }

    // Parse JSON — ArduinoJson tree needs ~4x raw JSON size
    // 9 bodies * ~600 bytes JSON = ~5400 raw, ~24KB parsed
    DynamicJsonDocument doc(24576);
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[AstroAPI] JSON error: %s\n", err.c_str());
        return result;
    }

    // Navigate: data.table.rows[] — each row is one body
    JsonArray rows = doc["data"]["table"]["rows"].as<JsonArray>();
    if (rows.isNull()) {
        Serial.println("[AstroAPI] No rows in response");
        return result;
    }

    result.count = 0;

    for (JsonObject row : rows) {
        const char *bodyId   = row["entry"]["id"]   | "";
        const char *bodyName = row["entry"]["name"] | "";

        // Get the first (and only) cell — we queried a single date/time
        JsonObject cell = row["cells"][0];
        if (cell.isNull()) continue;

        // degrees values come back as JSON strings ("38.5"), not numbers
        // atof() safely converts; returns 0.0 on empty string
        // Note: API uses "horizonal" (typo) — try both spellings
        const char *alt_s  = cell["position"]["horizonal"]["altitude"]["degrees"]
                          | (cell["position"]["horizontal"]["altitude"]["degrees"] | "0");
        const char *az_s   = cell["position"]["horizonal"]["azimuth"]["degrees"]
                          | (cell["position"]["horizontal"]["azimuth"]["degrees"]  | "0");
        const char *dist_s = cell["distance"]["fromEarth"]["au"] | "0";
        const char *con    = cell["position"]["constellation"]["name"] | "";

        float alt  = atof(alt_s);
        float az   = atof(az_s);
        float dist = atof(dist_s);

        // In the tabular response extraInfo values are JSON numbers (not strings)
        float mag  = cell["extraInfo"]["magnitude"]  | 99.0f;
        float elon = cell["extraInfo"]["elongation"] | 0.0f;

        // Handle Moon separately
        if (strcmp(bodyId, "moon") == 0) {
            result.moonAltDeg    = alt;
            result.moonAzDeg     = az;
            result.moonMagnitude = mag;
            strncpy(result.moonConstellation, con,
                    sizeof(result.moonConstellation) - 1);
            Serial.printf("[AstroAPI] Moon: alt=%.1f° az=%.1f°\n", alt, az);
            continue;
        }

        // Skip Sun — not displayed in planet list
        if (strcmp(bodyId, "sun") == 0) continue;

        // Match against our planet list
        for (int i = 0; i < ASTRO_PLANET_COUNT; i++) {
            if (strcmp(bodyId, PLANET_IDS[i]) == 0) {
                PlanetInfo &p = result.planets[result.count];
                strncpy(p.id,            PLANET_IDS[i],   sizeof(p.id)-1);
                strncpy(p.name,          PLANET_NAMES[i], sizeof(p.name)-1);
                p.altitudeDeg   = alt;
                p.azimuthDeg    = az;
                p.magnitude     = mag;
                p.elongationDeg = elon;
                p.distanceAu    = dist;
                strncpy(p.constellation, con, sizeof(p.constellation)-1);
                p.valid = true;
                result.count++;
                Serial.printf("[AstroAPI] %s: alt=%.1f° az=%.1f° mag=%.1f con=%s\n",
                              p.name, p.altitudeDeg, p.azimuthDeg, p.magnitude,
                              p.constellation[0] ? p.constellation : "none");
                break;
            }
        }
    }

    // Sort planets by altitude descending (highest/most visible first)
    for (int i = 0; i < result.count - 1; i++) {
        for (int j = i + 1; j < result.count; j++) {
            if (result.planets[j].altitudeDeg > result.planets[i].altitudeDeg) {
                PlanetInfo tmp    = result.planets[i];
                result.planets[i] = result.planets[j];
                result.planets[j] = tmp;
            }
        }
    }

    result.valid     = (result.count > 0);
    result.fetchedAt = millis();
    Serial.printf("[AstroAPI] OK — %d planets, moon alt=%.1f°\n",
                  result.count, result.moonAltDeg);
    return result;
}
