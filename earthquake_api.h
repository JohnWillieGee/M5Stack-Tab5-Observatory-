#pragma once
// =============================================================================
//  earthquake_api.h  —  USGS GeoJSON earthquake feed for Tab5 Weather Dashboard
//
//  Feed URL: https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_week.geojson
//  Free, no API key required. Returns all M2.5+ earthquakes, past 7 days, global.
//
//  We filter to Australia / Pacific region and keep the MAX_EQ most significant.
//  Refresh interval: 15 minutes (same as weather).
// =============================================================================

#include <Arduino.h>

#define EQ_MAX          30      // max earthquakes stored
#define EQ_PLACE_LEN    48      // max length of place string

// Region filter — matches earthquake screen map bounds
#define EQ_REGION_LON_MIN  108.0f
#define EQ_REGION_LON_MAX  182.0f
#define EQ_REGION_LAT_MIN  -55.0f
#define EQ_REGION_LAT_MAX   22.0f

// Sydney reference point for distance calculation
#define SYDNEY_LAT  -33.87f
#define SYDNEY_LON  151.21f

struct EqEvent {
    float   mag;                    // magnitude
    float   lat;                    // latitude (degrees)
    float   lon;                    // longitude (degrees)
    float   depth_km;               // focal depth in km
    int32_t age_minutes;            // minutes since event (from fetch time)
    char    place[EQ_PLACE_LEN];    // human-readable place string
    int32_t dist_km;                // distance from Sydney in km (-1 if not computed)
};

struct EqResult {
    EqEvent events[EQ_MAX];
    int     count;          // events in region
    int     total_global;   // total in feed (before region filter)

    // Precomputed summary counts for region (all time windows in the fetched data)
    int     count_m6plus;
    int     count_m5plus;
    int     count_m4plus;

    // Largest event index (into events[])
    int     largest_idx;

    bool    valid;
    char    fetch_time[20]; // "HH:MM DD/MM" of last successful fetch
};

// Fetch from USGS — call from loop(), runs synchronously (~1-2 seconds)
// Returns EqResult.valid=false on network/parse error.
EqResult eq_fetch();

// Human-readable depth classification
const char* eq_depth_label(float depth_km);

// Approximate great-circle distance Sydney -> (lat,lon) in km
int eq_dist_from_sydney(float lat, float lon);
