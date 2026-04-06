// =============================================================================
//  weather_api.cpp  —  WeatherAPI.com
//
//  Libraries needed:
//    • "ArduinoJson"  by Benoit Blanchon  (v6 or v7)
//    • WiFiClientSecure  (bundled with ESP32 Arduino core)
// =============================================================================

#include "weather_api.h"
#include "config.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// WeatherAPI endpoint — forecast.json gives current + forecast + astronomy
// days=3  aqi=no  alerts=no  keeps response small
static const char *URL_TMPL =
    "https://api.weatherapi.com/v1/forecast.json"
    "?key=%s&q=%s&days=3&aqi=yes&alerts=yes&lang=%s";

// ── Helpers ───────────────────────────────────────────────────────────────────

static const char* day_name_from_date(const char *date_str) {
    // date_str = "2025-06-14"
    struct tm t = {};
    sscanf(date_str, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
    t.tm_year -= 1900;
    t.tm_mon  -= 1;
    mktime(&t);
    static const char *days[] = {
        "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
    };
    return days[t.tm_wday];
}

const char* weather_wind_dir(int deg) {
    static const char *dirs[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    int idx = (int)((deg + 11.25f) / 22.5f) % 16;
    return dirs[idx];
}

// ── Main fetch ────────────────────────────────────────────────────────────────
WeatherResult weather_fetch() {
    // Use static storage — WeatherResult is too large for the loopTask stack.
    // Safe because weather_fetch() is only called from loop() (single-threaded).
    static WeatherResult result;
    memset(&result, 0, sizeof(result));
    result.valid = false;

    char url[256];
    snprintf(url, sizeof(url), URL_TMPL,
             WEATHER_API_KEY, WEATHER_LOCATION, WEATHER_LANG);

    WiFiClientSecure client;
    client.setInsecure();   // Skip cert verification — fine for hobby use.
                            // For production replace with root CA.
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(8000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Weather] HTTP error %d\n", code);
        http.end();
        return result;
    }

    String body = http.getString();
    http.end();

    // Parse — DynamicJsonDocument size tuned for this endpoint with 3 days
    DynamicJsonDocument doc(36864);  // 36KB — covers aqi + alerts payload
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[Weather] JSON error: %s\n", err.c_str());
        return result;
    }

    // ── Current ──────────────────────────────────────────────────────────────
    auto cur = doc["current"];
    result.tempC       = cur["temp_c"].as<float>();
    result.feelsLikeC  = cur["feelslike_c"].as<float>();
    result.humidity    = cur["humidity"].as<int>();
    result.windKph     = cur["wind_kph"].as<float>();
    result.windDeg     = cur["wind_degree"].as<int>();
    result.uvIndex     = cur["uv"].as<float>();
    result.precipMM    = cur["precip_mm"].as<float>();
    result.visKm       = cur["vis_km"].as<float>();
    result.pressureMb  = cur["pressure_mb"].as<float>();
    result.windGustKph = cur["gust_kph"].as<float>();
    result.cloudPct    = cur["cloud"].as<int>();
    result.isDay       = cur["is_day"].as<int>() == 1;
    strncpy(result.condition,  cur["condition"]["text"].as<const char*>(),  sizeof(result.condition) - 1);
    result.conditionCode = cur["condition"]["code"].as<int>();
    strncpy(result.windDir, weather_wind_dir(result.windDeg), sizeof(result.windDir) - 1);

    // ── Air quality (aqi=yes, free tier) ──────────────────────────────────────
    // Nested under current["air_quality"] — absent if aqi=no or data unavailable
    if (!cur["air_quality"].isNull()) {
        auto aqi = cur["air_quality"];
        result.aqiPm25  = aqi["pm2_5"].as<float>();
        result.aqiPm10  = aqi["pm10"].as<float>();
        result.aqiO3    = aqi["o3"].as<float>();
        result.aqiNo2   = aqi["no2"].as<float>();
        result.aqiUsEpa = aqi["us-epa-index"].as<int>();
        result.aqiValid = true;
        Serial.printf("[Weather] AQI: EPA=%d  PM2.5=%.1f  PM10=%.1f\n",
                      result.aqiUsEpa, result.aqiPm25, result.aqiPm10);
    }

    // ── Astronomy (today) ─────────────────────────────────────────────────────
    auto astro = doc["forecast"]["forecastday"][0]["astro"];
    strncpy(result.sunrise,   astro["sunrise"].as<const char*>(),   sizeof(result.sunrise)   - 1);
    strncpy(result.sunset,    astro["sunset"].as<const char*>(),    sizeof(result.sunset)    - 1);
    strncpy(result.moonrise,  astro["moonrise"].as<const char*>(),  sizeof(result.moonrise)  - 1);
    strncpy(result.moonset,   astro["moonset"].as<const char*>(),   sizeof(result.moonset)   - 1);
    strncpy(result.moonPhase, astro["moon_phase"].as<const char*>(),sizeof(result.moonPhase) - 1);
    result.moonIllumPct = astro["moon_illumination"].as<int>();

    // ── Forecast (3 days) ─────────────────────────────────────────────────────
    for (int i = 0; i < 3; i++) {
        auto fd   = doc["forecast"]["forecastday"][i];
        auto fday = fd["day"];
        ForecastDay &f = result.forecast[i];

        strncpy(f.date, fd["date"].as<const char*>(), sizeof(f.date) - 1);
        strncpy(f.dayName, day_name_from_date(f.date), sizeof(f.dayName) - 1);
        f.maxTempC      = fday["maxtemp_c"].as<float>();
        f.minTempC      = fday["mintemp_c"].as<float>();
        f.rainChancePct = fday["daily_chance_of_rain"].as<int>();
        f.precipMM      = fday["totalprecip_mm"].as<float>();
        f.maxWindKph    = fday["maxwind_kph"].as<float>();
        f.uvIndex       = fday["uv"].as<float>();
        f.avgHumidity   = fday["avghumidity"].as<int>();
        strncpy(f.condition, fday["condition"]["text"].as<const char*>(),
                sizeof(f.condition) - 1);
        f.conditionCode = fday["condition"]["code"].as<int>();

        // Per-day astronomy — sunrise/sunset for each forecast card
        auto fastro = fd["astro"];
        strncpy(f.sunrise, fastro["sunrise"].as<const char*>(), sizeof(f.sunrise) - 1);
        strncpy(f.sunset,  fastro["sunset"].as<const char*>(),  sizeof(f.sunset)  - 1);
    }

    // ── Hourly — next 6 slots from current hour ───────────────────────────────
    {
        time_t now_t  = time(nullptr);
        struct tm *lt = localtime(&now_t);
        int curHour   = lt->tm_hour;
        int filled    = 0;

        for (int day = 0; day < 2 && filled < 6; day++) {
            auto hours  = doc["forecast"]["forecastday"][day]["hour"];
            int  startH = (day == 0) ? curHour : 0;
            for (int h = startH; h < 24 && filled < 6; h++) {
                auto slot              = hours[h];
                HourlySlot &hs         = result.hourly[filled];
                hs.hour                = h;
                hs.tempC               = slot["temp_c"].as<float>();
                hs.rainChancePct       = slot["chance_of_rain"].as<int>();
                hs.conditionCode       = slot["condition"]["code"].as<int>();
                hs.isDay               = slot["is_day"].as<int>() == 1;
                filled++;
            }
        }
        result.hourlyCount = filled;
    }

    // ── Alerts / Warnings ────────────────────────────────────────────────────
    result.alertCount = 0;
    auto alertsArr = doc["alerts"]["alert"];
    if (!alertsArr.isNull()) {
        int n = alertsArr.size();
        if (n > WEATHER_MAX_ALERTS) n = WEATHER_MAX_ALERTS;
        for (int i = 0; i < n; i++) {
            auto a = alertsArr[i];
            AlertInfo &al = result.alerts[result.alertCount++];
            strncpy(al.headline, a["headline"].as<const char*>() ? a["headline"].as<const char*>() : "",
                    sizeof(al.headline) - 1);
            strncpy(al.severity, a["severity"].as<const char*>() ? a["severity"].as<const char*>() : "",
                    sizeof(al.severity) - 1);
            strncpy(al.urgency,  a["urgency"].as<const char*>()  ? a["urgency"].as<const char*>()  : "",
                    sizeof(al.urgency)  - 1);
            strncpy(al.areas,    a["areas"].as<const char*>()    ? a["areas"].as<const char*>()    : "",
                    sizeof(al.areas)   - 1);
            strncpy(al.onset,    a["effective"].as<const char*>() ? a["effective"].as<const char*>() : "",
                    sizeof(al.onset)   - 1);
            strncpy(al.expires,  a["expires"].as<const char*>()  ? a["expires"].as<const char*>()  : "",
                    sizeof(al.expires) - 1);
            strncpy(al.event,    a["event"].as<const char*>()    ? a["event"].as<const char*>()    : "",
                    sizeof(al.event)   - 1);
            // desc — truncate to fit our buffer
            const char *d = a["desc"].as<const char*>();
            if (d) strncpy(al.desc, d, sizeof(al.desc) - 1);
        }
        Serial.printf("[Weather] Alerts: %d active\n", result.alertCount);
    }
    if (result.alertCount == 0)
        Serial.println("[Weather] Alerts: none");

    result.valid     = true;
    result.fetchedAt = millis();
    Serial.printf("[Weather] OK - %.1f C, %s\n", result.tempC, result.condition);
    return result;
}
