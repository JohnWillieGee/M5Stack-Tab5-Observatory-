#pragma once
// =============================================================================
//  weather_api.h  —  WeatherAPI.com  (current + 3-day forecast + astronomy)
//  Free tier: https://www.weatherapi.com  (1M calls/month)
// =============================================================================

#include <Arduino.h>

// ── Data structures ───────────────────────────────────────────────────────────

struct HourlySlot {
    int   hour;              // 0-23
    float tempC;
    int   rainChancePct;
    int   conditionCode;
    bool  isDay;
};

struct ForecastDay {
    char  date[12];          // "2025-06-14"
    char  dayName[12];       // "Saturday"
    float maxTempC;
    float minTempC;
    int   rainChancePct;
    float precipMM;
    float maxWindKph;        // max wind speed for the day
    float uvIndex;           // UV index for the day
    int   avgHumidity;       // average humidity %
    char  condition[48];
    int   conditionCode;     // WeatherAPI condition code
    bool  isDay;
    char  sunrise[10];       // "06:42 AM"
    char  sunset[10];        // "07:58 PM"
};

// ── Weather alert/warning ─────────────────────────────────────────────────────
#define WEATHER_MAX_ALERTS  5

struct AlertInfo {
    char headline[80];     // Short title e.g. "Severe Thunderstorm Warning"
    char severity[20];     // "Moderate", "Severe", "Extreme"
    char urgency[20];      // "Immediate", "Expected", "Future"
    char areas[48];        // affected areas (may be empty)
    char onset[20];        // "2026-03-18 14:00"
    char expires[20];      // "2026-03-18 20:00"
    char desc[180];        // description text (truncated to fit)
    char event[48];        // event type e.g. "Severe Thunderstorm Warning"
};

struct WeatherResult {
    // Current conditions
    float  tempC;
    float  feelsLikeC;
    int    humidity;
    float  windKph;
    int    windDeg;
    char   windDir[8];       // "SW", "NNE" etc.
    float  uvIndex;
    float  precipMM;
    float  visKm;
    float  pressureMb;       // current pressure in hPa/mb
    float  windGustKph;      // current wind gust speed
    int    cloudPct;         // cloud cover percentage
    // Air quality (aqi=yes in request, free tier)
    float  aqiPm25;          // PM2.5 µg/m³
    float  aqiPm10;          // PM10  µg/m³
    float  aqiO3;            // Ozone µg/m³
    float  aqiNo2;           // NO2   µg/m³
    int    aqiUsEpa;         // US EPA index 1-6 (1=Good…6=Hazardous)
    bool   aqiValid;         // true if aqi data was returned
    char   condition[48];
    int    conditionCode;
    bool   isDay;

    // Astronomy (today)
    char   sunrise[10];      // "06:42 AM\0" needs 9 bytes
    char   sunset[10];
    char   moonrise[10];
    char   moonset[10];
    char   moonPhase[24];    // "Waxing Gibbous"
    int    moonIllumPct;

    // Weather alerts/warnings (alerts=yes, free tier)
    AlertInfo alerts[WEATHER_MAX_ALERTS];
    int       alertCount;   // 0 = no active alerts

    // 3-day forecast
    ForecastDay forecast[3];

    // Hourly - next 6 slots from current hour
    HourlySlot  hourly[6];
    int         hourlyCount; // how many slots were populated (usually 6)

    bool   valid;
    unsigned long fetchedAt; // millis() when last fetched
};

// ── Public API ────────────────────────────────────────────────────────────────

WeatherResult weather_fetch();
const char* weather_wind_dir(int degrees);
