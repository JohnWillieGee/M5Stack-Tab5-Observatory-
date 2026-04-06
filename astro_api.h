#pragma once
// =============================================================================
//  astro_api.h  —  AstronomyAPI.com  /api/v2/bodies/positions
//
//  Single GET request returns altitude, azimuth, magnitude, elongation and
//  constellation for Sun, Moon and all 7 planets at Sydney's location.
//
//  Auth: Basic  base64("applicationId:applicationSecret") — hardcoded in config.h
//  Fetch interval: once per 6 hours (planets move slowly)
//  JSON doc: ~8 KB
// =============================================================================

#include <Arduino.h>

// ── Number of bodies tracked ──────────────────────────────────────────────────
#define ASTRO_PLANET_COUNT  7   // Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune

struct PlanetInfo {
    char   id[12];           // "mercury", "venus" …
    char   name[12];         // "Mercury", "Venus" …
    float  altitudeDeg;      // degrees above horizon (negative = below)
    float  azimuthDeg;       // compass bearing 0-360°
    float  magnitude;        // apparent magnitude (lower = brighter)
    float  elongationDeg;    // angular separation from Sun
    char   constellation[24];// e.g. "Taurus"
    float  distanceAu;       // distance from Earth in AU
    bool   valid;
};

struct AstroResult {
    PlanetInfo planets[ASTRO_PLANET_COUNT];
    int        count;        // how many valid entries
    // Moon extras (returned alongside planets)
    float  moonAltDeg;
    float  moonAzDeg;
    float  moonMagnitude;
    char   moonConstellation[24];
    // Fetch metadata
    bool          valid;
    unsigned long fetchedAt;  // millis()
};

// ── Public API ────────────────────────────────────────────────────────────────
// Fetches planet positions for today at the configured observer location.
// time_str: local time to query, e.g. "21:00:00" — use tonight's viewing time.
AstroResult astro_fetch(const char *date_str, const char *time_str);
