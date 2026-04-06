// =============================================================================
//  ui_main.cpp  —  LVGL UI for Tab5 Weather Dashboard
//  Display: 1280 × 720 px
//
//  Layout philosophy: dark background, large readable text, minimal chrome.
//  Status bar (40 px) sits at the top of every screen.
//  Left/right arrow buttons navigate between screens.
// =============================================================================

#include "ui_main.h"
#include "config.h"
#include "space_weather_api.h"
#include "astro_api.h"
#include "bom_warnings_api.h"
#include "earthquake_api.h"
#include "earthquake_map_data.h"
#include <Preferences.h>  // for settings_save()
#include <M5Unified.h>  // safe here — M5.begin() already called in .ino setup()
#include <WiFi.h>
#include <math.h>
#include <SPI.h>          // required for SD File type in tides screen
#include <SD.h>           // required for SD File type in tides screen

// =============================================================================
//  COLOUR PALETTE  (all via lv_color_hex)
// =============================================================================
#define C_BG        lv_color_hex(0x0D1117)  // near-black
#define C_CARD      lv_color_hex(0x161B22)  // card background
#define C_BORDER    lv_color_hex(0x30363D)  // subtle border
#define C_TEXT      lv_color_hex(0xE6EDF3)  // primary text
#define C_DIM       lv_color_hex(0x8B949E)  // secondary text
#define C_ACCENT    lv_color_hex(0x58A6FF)  // blue accent
#define C_WARM      lv_color_hex(0xFF7B50)  // warm orange (temp)
#define C_COOL      lv_color_hex(0x79C0FF)  // cool blue (humidity)
#define C_GREEN     lv_color_hex(0x3FB950)  // good / connected
#define C_YELLOW    lv_color_hex(0xD29922)  // warning
#define C_RED       lv_color_hex(0xF85149)  // error / low battery

// =============================================================================
//  LAYOUT CONSTANTS
// =============================================================================
#define STATUSBAR_H  44
#define NAV_BTN_W    70
#define SCREEN_W     1280
#define SCREEN_H     720
#define CONTENT_Y    STATUSBAR_H
#define CONTENT_H    (SCREEN_H - STATUSBAR_H)
#define PAD          24

// =============================================================================
//  GLOBALS — screens and shared widgets
// =============================================================================
static lv_obj_t *screens[SCREEN_COUNT] = {};
static int        cur_screen      = SCREEN_CLOCK;

// Status bar widgets — one per screen, populated in build_statusbar()
// (arrays declared alongside build_statusbar definition below)

// =============================================================================
//  SCREEN 0 — Combined Clock + Indoor Sensors + Today's Weather
// =============================================================================
// ── Clock column ──────────────────────────────────────────────────────────────
static lv_obj_t  *clk_hh          = nullptr;   // "HH" segment
static lv_obj_t  *clk_col1        = nullptr;   // first ":"
static lv_obj_t  *clk_mm          = nullptr;   // "MM" segment
static lv_obj_t  *clk_col2        = nullptr;   // second ":"
static lv_obj_t  *clk_ss          = nullptr;   // "SS" segment
static lv_obj_t  *clk_date        = nullptr;
static lv_obj_t  *clk_tz          = nullptr;

// ── Indoor sensor row (right panel, top) ─────────────────────────────────────
static lv_obj_t  *clk_s_temp      = nullptr;
static lv_obj_t  *clk_s_hum       = nullptr;
static lv_obj_t  *clk_s_press     = nullptr;

// ── Today's weather panel (right panel, below sensors) ────────────────────────
static lv_obj_t  *clk_wx_temp     = nullptr;
static lv_obj_t  *clk_wx_feels    = nullptr;
static lv_obj_t  *clk_wx_cond     = nullptr;   // primary condition line
static lv_obj_t  *clk_wx_desc1    = nullptr;   // description line 1
static lv_obj_t  *clk_wx_desc2    = nullptr;   // description line 2
static lv_obj_t  *clk_wx_wind     = nullptr;
static lv_obj_t  *clk_wx_hum      = nullptr;
static lv_obj_t  *clk_wx_uv       = nullptr;
static lv_obj_t  *clk_wx_vis      = nullptr;
static lv_obj_t  *clk_wx_rain     = nullptr;
static lv_obj_t  *clk_wx_sun      = nullptr;   // sunrise / sunset (two lines)
static lv_obj_t  *clk_wx_forecast = nullptr;   // tomorrow + day-after paragraph
static lv_obj_t  *clk_wx_updated  = nullptr;

// ── 5-slot weather outlook strip (bottom of left column) ─────────────────────
#define OUTLOOK_SLOTS  5
static lv_obj_t    *out_canvas[OUTLOOK_SLOTS] = {};   // icon canvas per slot
static lv_color_t  *out_canvas_buf[OUTLOOK_SLOTS] = {};
static lv_obj_t    *out_day[OUTLOOK_SLOTS]    = {};   // day name label
static lv_obj_t    *out_temp[OUTLOOK_SLOTS]   = {};   // hi/lo label
static lv_obj_t    *out_rain[OUTLOOK_SLOTS]   = {};   // rain % label

// Screen 1 — Local (BME688)
static lv_obj_t  *loc_temp        = nullptr;
static lv_obj_t  *loc_temp_sub    = nullptr;   // offset note
static lv_obj_t  *loc_hum         = nullptr;
static lv_obj_t  *loc_hum_sub     = nullptr;   // comfort label
static lv_obj_t  *loc_press       = nullptr;
static lv_obj_t  *loc_trend       = nullptr;
// trend chart canvas
static lv_obj_t  *loc_chart_canvas  = nullptr;
static lv_color_t *loc_chart_buf    = nullptr;
static lv_obj_t  *loc_wait_lbl      = nullptr;   // "Collecting data" — hidden once data arrives
#define LOC_CHART_W  802   // RIGHT_W(850) - PAD*2
#define LOC_CHART_H  280   // CHART_CARD_H(361) - PAD*2 - header(20) - small margin
// comfort panel
static lv_obj_t  *loc_dewpoint    = nullptr;
static lv_obj_t  *loc_heatidx     = nullptr;
static lv_obj_t  *loc_comfort     = nullptr;
static lv_obj_t  *loc_dewbar      = nullptr;   // bar fill object
static lv_obj_t  *loc_heatbar     = nullptr;
static lv_obj_t  *loc_comfbar     = nullptr;
// keep these for update function compatibility (no-ops if null-checked)
static lv_obj_t  *loc_iaq           = nullptr;
static lv_obj_t  *loc_iaq_label     = nullptr;
static lv_obj_t  *loc_iaq_acc_badge = nullptr;
static lv_obj_t  *loc_iaq_bar_fill  = nullptr;
static lv_obj_t  *loc_iaq_resist    = nullptr;
static lv_obj_t  *loc_iaq_base      = nullptr;
static lv_obj_t  *loc_iaq_scale_lbl = nullptr;
static lv_obj_t  *loc_co2         = nullptr;
static lv_obj_t  *loc_voc         = nullptr;
static lv_obj_t  *loc_accuracy    = nullptr;

// Screen 2 — Weather current
static lv_obj_t  *wx_temp         = nullptr;
static lv_obj_t  *wx_feels        = nullptr;
static lv_obj_t  *wx_cond         = nullptr;
static lv_obj_t  *wx_wind         = nullptr;
static lv_obj_t  *wx_hum          = nullptr;
static lv_obj_t  *wx_uv           = nullptr;
static lv_obj_t  *wx_updated      = nullptr;
// expanded fields
static lv_obj_t  *wx_vis          = nullptr;
static lv_obj_t  *wx_precip       = nullptr;
static lv_obj_t  *wx_pressure     = nullptr;
static lv_obj_t  *wx_pressure_trend = nullptr;
static lv_obj_t  *wx_dewpoint     = nullptr;
static lv_obj_t  *wx_cloud        = nullptr;
static lv_obj_t  *wx_aqi_cat      = nullptr;   // AQI category "Good" / "Moderate" etc
static lv_obj_t  *wx_aqi_epa      = nullptr;   // EPA index "EPA 1/6"
static lv_obj_t  *wx_aqi_pm25     = nullptr;   // PM2.5 value
static lv_obj_t  *wx_aqi_pm10     = nullptr;   // PM10 value
static lv_obj_t  *wx_aqi_o3       = nullptr;   // O3 value
static lv_obj_t  *wx_aqi_no2      = nullptr;   // NO2 value
static lv_obj_t  *wx_wind_gust    = nullptr;  // gust sub-label on wind card
static lv_obj_t  *wx_hum_hero     = nullptr;  // humidity value in hero mid-zone
// hero icon canvas
static lv_obj_t  *wx_icon_canvas  = nullptr;
static lv_color_t *wx_icon_buf    = nullptr;
// UV card
static lv_obj_t  *wx_uv_num       = nullptr;
static lv_obj_t  *wx_uv_cat       = nullptr;
static lv_obj_t  *wx_uv_peak      = nullptr;
static lv_obj_t  *wx_uv_marker    = nullptr;
static lv_obj_t  *wx_uv_advice    = nullptr;
static int        wx_uv_bar_w     = 0;     // bar pixel width, set during build
// bottom-right detail card
static lv_obj_t  *wx_sunrise      = nullptr;
static lv_obj_t  *wx_sunrise_sub  = nullptr;
static lv_obj_t  *wx_solarnoon    = nullptr;
static lv_obj_t  *wx_solarnoon_sub = nullptr;
static lv_obj_t  *wx_sunset       = nullptr;
static lv_obj_t  *wx_sunset_sub   = nullptr;
static lv_obj_t  *wx_moon_phase   = nullptr;
static lv_obj_t  *wx_moon_sub     = nullptr;
static lv_obj_t  *wx_next_event   = nullptr;
static lv_obj_t  *wx_next_event_sub = nullptr;
static lv_obj_t  *wx_season       = nullptr;
// hourly strip — icon canvas matches OUTLOOK_ICON_W/H so draw_wx_icon works correctly
#define WX_HOURLY_SLOTS  6
#define WX_HOURLY_ICON_W OUTLOOK_ICON_W
#define WX_HOURLY_ICON_H OUTLOOK_ICON_H
static lv_obj_t   *wx_h_card[WX_HOURLY_SLOTS]   = {};
static lv_obj_t   *wx_h_time[WX_HOURLY_SLOTS]   = {};
static lv_obj_t   *wx_h_canvas[WX_HOURLY_SLOTS] = {};
static lv_color_t *wx_h_buf[WX_HOURLY_SLOTS]    = {};
static lv_obj_t   *wx_h_temp[WX_HOURLY_SLOTS]   = {};
static lv_obj_t   *wx_h_rain[WX_HOURLY_SLOTS]   = {};
// sparkline canvas — drawn below the hourly slot cards
#define WX_SPARK_H  34   // height of sparkline strip below cards
static lv_obj_t   *wx_spark_canvas = nullptr;
static lv_color_t *wx_spark_buf    = nullptr;
static int         wx_spark_w      = 0;   // set during build, used during draw

// Screen 3 — Forecast
static lv_obj_t  *fc_cards[3]     = {};

// Screen 4 — Astronomy
static lv_obj_t  *as_sunrise       = nullptr;
static lv_obj_t  *as_sunrise_sub   = nullptr;
static lv_obj_t  *as_solarnoon     = nullptr;
static lv_obj_t  *as_solarnoon_sub = nullptr;
static lv_obj_t  *as_sunset        = nullptr;
static lv_obj_t  *as_sunset_sub    = nullptr;
static lv_obj_t  *as_daylen        = nullptr;
static lv_obj_t  *as_next_event    = nullptr;
static lv_obj_t  *as_next_event_sub= nullptr;
static lv_obj_t  *as_season        = nullptr;
// sun arc canvas
static lv_obj_t  *as_sun_canvas    = nullptr;
static lv_color_t *as_sun_buf      = nullptr;
#define AS_ARC_W   492   // inner card width
#define AS_ARC_H   178   // top section minus header
// moon
static lv_obj_t  *as_moon_canvas   = nullptr;
static lv_color_t *as_moon_buf     = nullptr;
#define AS_MOON_D  110
static lv_obj_t  *as_moon_phase    = nullptr;
static lv_obj_t  *as_moon_illum    = nullptr;
static lv_obj_t  *as_moon_age      = nullptr;
static lv_obj_t  *as_moonrise      = nullptr;
static lv_obj_t  *as_moonrise_sub  = nullptr;
static lv_obj_t  *as_moonset       = nullptr;
static lv_obj_t  *as_moonset_sub   = nullptr;
static lv_obj_t  *as_moon_illum_pct= nullptr;
static lv_obj_t  *as_moon_age_val  = nullptr;
static lv_obj_t  *as_next_full     = nullptr;
static lv_obj_t  *as_next_new      = nullptr;

// =============================================================================
//  SCREEN 5 — Solar Elevation Chart
// =============================================================================
#define SOL_CHART_W  1140   // SCREEN_W - 2*NAV_BTN_W
#define SOL_CHART_H  500    // canvas height — fills CHART_CARD_H (~506px)
static lv_obj_t   *sol_canvas     = nullptr;
static lv_color_t *sol_canvas_buf = nullptr;
// Stat labels (updated each minute)
static lv_obj_t   *sol_sunrise    = nullptr;
static lv_obj_t   *sol_sunrise_cd = nullptr;
static lv_obj_t   *sol_noon       = nullptr;
static lv_obj_t   *sol_noon_cd    = nullptr;
static lv_obj_t   *sol_sunset     = nullptr;
static lv_obj_t   *sol_sunset_cd  = nullptr;
static lv_obj_t   *sol_daylen     = nullptr;
static lv_obj_t   *sol_peak       = nullptr;
static lv_obj_t   *sol_cur_elev   = nullptr;
static lv_obj_t   *sol_cur_az     = nullptr;

// Screen 6 — Seasons orbit (LVGL canvas drawn at runtime)
static lv_obj_t  *sea_canvas      = nullptr;
static lv_obj_t  *sea_season      = nullptr;
static lv_obj_t  *sea_next_event  = nullptr;
static lv_obj_t  *sea_earth_dot   = nullptr;   // animated overlay dot on Earth position
static int        sea_pulse_phase  = 0;          // 0-3 step for pulse animation
// Per-quadrant season progress overlays (updated each minute in render_seasons)
static lv_obj_t  *sea_q_dates[4]  = {};         // "22 Dec → 20 Mar"
static lv_obj_t  *sea_q_status[4] = {};         // "Day 42 of 89  ·  47 left" / "in 62 days"

// Screen 7 — Lunar orbit (LVGL canvas drawn at runtime)
static lv_obj_t  *lun_canvas      = nullptr;
// Lunar overlay labels — updated each minute by render_lunar
static lv_obj_t  *lun_phase       = nullptr;   // current phase name (large)
static lv_obj_t  *lun_illum       = nullptr;   // illumination % value
static lv_obj_t  *lun_illum_bar   = nullptr;   // illumination bar fill
static lv_obj_t  *lun_age         = nullptr;   // age in days
static lv_obj_t  *lun_next        = nullptr;   // next phase + days
static lv_obj_t  *lun_moonrise    = nullptr;
static lv_obj_t  *lun_moonrise_sub= nullptr;   // countdown
static lv_obj_t  *lun_moonset     = nullptr;
static lv_obj_t  *lun_moonset_sub = nullptr;
static lv_obj_t  *lun_full        = nullptr;   // days to full
static lv_obj_t  *lun_new         = nullptr;   // days to new

// =============================================================================
//  SCREEN 8 — Planets Tonight (AstronomyAPI)
// =============================================================================
#define PL_COMPASS_W  608   // compass canvas width  (RIGHT_W - PAD*2)
#define PL_COMPASS_H  430   // compass canvas height — matches COMPASS_CARD_H - label
static lv_obj_t   *pl_compass_canvas = nullptr;
static lv_color_t *pl_compass_buf    = nullptr;
// Per-planet widgets in the list (7 planets)
static lv_obj_t   *pl_name[ASTRO_PLANET_COUNT]   = {};
static lv_obj_t   *pl_pill[ASTRO_PLANET_COUNT]   = {};   // visibility pill
static lv_obj_t   *pl_bar[ASTRO_PLANET_COUNT]    = {};   // altitude fill bar
static lv_obj_t   *pl_alt[ASTRO_PLANET_COUNT]    = {};   // altitude value
static lv_obj_t   *pl_az[ASTRO_PLANET_COUNT]     = {};   // azimuth + direction
static lv_obj_t   *pl_mag[ASTRO_PLANET_COUNT]    = {};   // magnitude
static lv_obj_t   *pl_con[ASTRO_PLANET_COUNT]    = {};   // constellation
// Summary / footer labels
static lv_obj_t   *pl_best        = nullptr;    // "Best tonight: Venus & Jupiter"
static lv_obj_t   *pl_best_sub    = nullptr;
static lv_obj_t   *pl_updated     = nullptr;

// =============================================================================
//  SCREEN 9 — Space Weather (BOM SWS)  [was 8]
// =============================================================================
// Canvas for K-index bar chart — taller now to fill the chart card
static lv_obj_t  *sw_canvas       = nullptr;
static lv_color_t *sw_canvas_buf  = nullptr;
#define SW_CHART_W  440
// SW_CHART_H is computed at build time from CHART_CARD_H; use a generous fixed value
// CHART_CARD_H = CH - K_HERO_H - PAD*3 = 650-160-72 = 418; minus label(20)+pad(24) = 374
#define SW_CHART_H  374

// Aurora / alert panel (left column)
static lv_obj_t  *sw_alert_badge  = nullptr;   // big status badge label
static lv_obj_t  *sw_alert_band   = nullptr;   // lat_band label
static lv_obj_t  *sw_alert_desc   = nullptr;   // aurora description text
static lv_obj_t  *sw_mag_status   = nullptr;   // mag alert status pill label
static lv_obj_t  *sw_mag_desc     = nullptr;   // mag alert full description text (NEW)
static lv_obj_t  *sw_geo_pill     = nullptr;   // geo warning status pill label (NEW)
static lv_obj_t  *sw_geo_desc     = nullptr;   // geo warning full description text (NEW)

// K-index (centre column)
static lv_obj_t  *sw_k_value      = nullptr;   // big current K number
static lv_obj_t  *sw_k_label      = nullptr;   // "Quiet" / "Storm G1" etc.
static lv_obj_t  *sw_k_time       = nullptr;   // timestamp

// Right column indices
static lv_obj_t  *sw_a_value      = nullptr;
static lv_obj_t  *sw_a_label      = nullptr;
static lv_obj_t  *sw_a_bar        = nullptr;   // A-index bar fill
static lv_obj_t  *sw_dst_value    = nullptr;
static lv_obj_t  *sw_dst_label    = nullptr;
static lv_obj_t  *sw_geo_status   = nullptr;   // geo status on right card (combined)
static lv_obj_t  *sw_updated      = nullptr;

// =============================================================================
//  SCREEN 11 — Sensor Stats (tabbed temp/hum/pressure, 8h/24h/7d)
// =============================================================================
// Chart canvas — full width of content area minus stats column
#define SS_CHART_W   800    // chart canvas pixel width
#define SS_CHART_H   490    // chart canvas pixel height
#define SS_STATS_COL  260   // right stats column width
#define SS_CONTENT_X  NAV_BTN_W
#define SS_CONTENT_W  (SCREEN_W - NAV_BTN_W * 2)   // 1140

// Tab / window state
static int  ss_tab    = 0;   // 0=Temp  1=Humidity  2=Pressure
static int  ss_window = 0;   // 0=8h  1=24h  2=7d

// Tab buttons
static lv_obj_t *ss_tab_btn[3]  = {};
// Window buttons
static lv_obj_t *ss_win_btn[3]  = {};
// Chart canvas
static lv_obj_t    *ss_canvas   = nullptr;
static lv_color_t  *ss_buf      = nullptr;
// Chart header labels
static lv_obj_t *ss_cur_val     = nullptr;   // large current reading
static lv_obj_t *ss_hi_lo       = nullptr;   // "Hi x  Lo x  Avg x" right side
static lv_obj_t *ss_avg_rng     = nullptr;   // "Avg x  Range x" second line
// Stats column cards  (today / yesterday / 7d / trend)
static lv_obj_t *ss_today_val   = nullptr;
static lv_obj_t *ss_today_hi    = nullptr;
static lv_obj_t *ss_today_lo    = nullptr;
static lv_obj_t *ss_today_avg   = nullptr;
static lv_obj_t *ss_yest_val    = nullptr;
static lv_obj_t *ss_yest_hi     = nullptr;
static lv_obj_t *ss_yest_lo     = nullptr;
static lv_obj_t *ss_yest_avg    = nullptr;
static lv_obj_t *ss_7d_hi       = nullptr;
static lv_obj_t *ss_7d_lo       = nullptr;
static lv_obj_t *ss_7d_avg      = nullptr;
static lv_obj_t *ss_7d_trend    = nullptr;
static lv_obj_t *ss_trend_dir   = nullptr;   // "Rising / Falling / Steady"
static lv_obj_t *ss_trend_rate  = nullptr;   // "+0.4/hr"
// Extended history buffers (heap-allocated on first 24h/7d fetch)
static float *ss_ext_temp  = nullptr;
static float *ss_ext_hum   = nullptr;
static float *ss_ext_pres  = nullptr;
static int    ss_ext_count = 0;
static int    ss_ext_window = -1;   // which window the ext data was loaded for
static SdExtResult ss_ext_stats = {};  // cached stats from last extended read

// =============================================================================
//  SCREEN 9 — Settings + System Info
// =============================================================================
// ── Settings live values (volatile — written by UI, read by loop()) ───────────
volatile int  g_setting_brightness       = 180;
volatile int  g_setting_timeout_sec      = 300;
volatile bool g_setting_dim_before_sleep = true;
volatile int  g_setting_wx_interval_sec  = 900;
volatile int  g_setting_sw_interval_sec  = 900;
volatile bool g_setting_wake_to_clock    = false;
volatile bool g_force_wx_refresh         = false;
volatile bool g_force_sw_refresh         = false;

// ── Timeout / wake state (managed by ui_notify_touch + loop) ─────────────────
// Non-static so the main .ino loop can read/write via extern declarations
unsigned long s_last_touch_ms = 0;       // millis() of last touch event
bool          s_display_dimmed = false;   // true when dimmed to warning level
bool          s_display_off    = false;   // true when blanked (brightness=0)
static int    s_pre_dim_brightness = 180; // saved brightness before dim

// ── Settings screen LVGL widgets ─────────────────────────────────────────────
// Brightness row buttons (5 steps)
#define BRIGHT_STEPS 5
static const int BRIGHT_VALS[BRIGHT_STEPS] = {30, 80, 130, 180, 230};
static lv_obj_t *set_bright_btns[BRIGHT_STEPS] = {};

// Timeout row buttons (6 options)
#define TIMEOUT_OPTS 6
static const int TIMEOUT_VALS[TIMEOUT_OPTS] = {0, 60, 120, 300, 600, 1800};
static const char *TIMEOUT_LBLS[TIMEOUT_OPTS] = {"Off","1m","2m","5m","10m","30m"};
static lv_obj_t *set_timeout_btns[TIMEOUT_OPTS] = {};

// Dim toggle
static lv_obj_t *set_dim_on  = nullptr;
static lv_obj_t *set_dim_off = nullptr;

// Weather refresh (4 options)
#define WX_OPTS 4
static const int WX_VALS[WX_OPTS] = {300, 600, 900, 1800};
static const char *WX_LBLS[WX_OPTS] = {"5m","10m","15m","30m"};
static lv_obj_t *set_wx_btns[WX_OPTS] = {};

// Space wx refresh (3 options)
#define SW_OPTS 3
static const int SW_VALS[SW_OPTS] = {900, 1800, 3600};
static const char *SW_LBLS[SW_OPTS] = {"15m","30m","1h"};
static lv_obj_t *set_sw_btns[SW_OPTS] = {};

// Wake screen (2 options)
static lv_obj_t *set_wake_last  = nullptr;
static lv_obj_t *set_wake_clock = nullptr;

// System info labels (right column, updated by ui_update_settings_sysinfo)
static lv_obj_t *sys_bat_status  = nullptr;
static lv_obj_t *sys_bat_level   = nullptr;
static lv_obj_t *sys_bat_voltage = nullptr;
static lv_obj_t *sys_wifi_ssid   = nullptr;
static lv_obj_t *sys_wifi_rssi   = nullptr;
static lv_obj_t *sys_wifi_chan   = nullptr;
static lv_obj_t *sys_wifi_ip     = nullptr;
static lv_obj_t *sys_heap        = nullptr;
static lv_obj_t *sys_psram       = nullptr;
static lv_obj_t *sys_uptime      = nullptr;
static lv_obj_t *sys_chip        = nullptr;
static lv_obj_t *sys_fetch_wx    = nullptr;
static lv_obj_t *sys_fetch_sw    = nullptr;
static lv_obj_t *sys_fetch_ntp   = nullptr;

// =============================================================================
//  HELPER — styled label factory
// =============================================================================
static lv_obj_t* make_label(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, lv_color_t color,
                              lv_align_t align, int x, int y) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_align(l, align, x, y);
    return l;
}

// Card container
static lv_obj_t* make_card(lv_obj_t *parent,
                             int x, int y, int w, int h) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, C_BORDER, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, PAD, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

// Card with zero internal padding — for screens that position children absolutely
static lv_obj_t* make_card_nopad(lv_obj_t *parent, int x, int y, int w, int h,
                                   lv_color_t border_col = C_BORDER) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, border_col, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

// Navigation arrow button — transparent, full-height touch zone
static lv_obj_t* make_nav_btn(lv_obj_t *parent, const char *symbol,
                                lv_align_t align,
                                lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, NAV_BTN_W, CONTENT_H);
    lv_obj_align(b, align, 0, STATUSBAR_H);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, C_DIM, 0);
    lv_obj_center(lbl);
    return b;
}

// =============================================================================
//  NAVIGATION SYSTEM
//  Three complementary methods:
//    1. Tap left/right arrow buttons (always visible, full-height touch zone)
//    2. Swipe left/right anywhere on the screen content area
//    3. Long-press anywhere to open the quick-pick menu overlay
// =============================================================================

// ── Dot indicator ─────────────────────────────────────────────────────────────
#define DOT_R        6
#define DOT_GAP      18
#define DOT_Y        (SCREEN_H - 16)

// All dots on all screens — [screen_index][dot_index]
static lv_obj_t *all_dots[SCREEN_COUNT][SCREEN_COUNT] = {};

static void update_dots(int active) {
    for (int s = 0; s < SCREEN_COUNT; s++) {
        for (int d = 0; d < SCREEN_COUNT; d++) {
            if (!all_dots[s][d]) continue;
            lv_obj_set_style_bg_color(all_dots[s][d],
                d == active ? C_TEXT : C_BORDER, 0);
        }
    }
}

// ── Screen names for quick-pick menu ─────────────────────────────────────────
// Indices 0–13 match SCREEN_CLOCK through SCREEN_EARTHQUAKE.
// SCREEN_SETTINGS (14) is excluded from this array — it has its own footer button.
static const char *SCREEN_NAMES[SCREEN_COUNT - 1] = {
    LV_SYMBOL_HOME    " Clock",
    LV_SYMBOL_WIFI    " Local Sensor",
    LV_SYMBOL_IMAGE   " Weather",
    LV_SYMBOL_LIST    " Forecast",
    LV_SYMBOL_LOOP    " Astronomy",
    LV_SYMBOL_LOOP    " Solar Elevation",
    LV_SYMBOL_LOOP    " Seasons",
    LV_SYMBOL_LOOP    " Lunar Orbit",
    LV_SYMBOL_LOOP    " Planets Tonight",
    LV_SYMBOL_WARNING " Alerts",
    LV_SYMBOL_WARNING " Space Weather",
    LV_SYMBOL_CHARGE  " Sensor History",
    LV_SYMBOL_DRIVE   " Tides - Fort Denison",
    LV_SYMBOL_WARNING " Earthquake Activity",
};

// ── Quick-pick overlay ────────────────────────────────────────────────────────
static lv_obj_t *qp_overlay = nullptr;

static void qp_btn_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (qp_overlay) {
        lv_obj_del(qp_overlay);
        qp_overlay = nullptr;
    }
    ui_show_screen(idx);
}

static void qp_bg_cb(lv_event_t *e) {
    if (qp_overlay) {
        lv_obj_del(qp_overlay);
        qp_overlay = nullptr;
    }
}

static void open_quick_pick() {
    if (qp_overlay) return;

    lv_obj_t *scr = screens[cur_screen];
    qp_overlay = lv_obj_create(scr);
    lv_obj_set_size(qp_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_pos(qp_overlay, 0, 0);
    lv_obj_set_style_bg_color(qp_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(qp_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(qp_overlay, 0, 0);
    lv_obj_set_style_radius(qp_overlay, 0, 0);
    lv_obj_clear_flag(qp_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(qp_overlay, qp_bg_cb, LV_EVENT_CLICKED, nullptr);

    // ── Panel — 2-column grid, fits all 10 content screens + settings footer ──
    const int MW     = 700;
    const int MBTN_H = 52;
    const int MPAD   = 10;
    const int COLS   = 2;
    const int NAV_CNT = SCREEN_COUNT - 1;          // 10 swipeable screens
    const int ROWS   = (NAV_CNT + COLS - 1) / COLS; // ceil(10/2) = 5
    const int BTN_W  = (MW - MPAD * (COLS + 1)) / COLS;  // (700-30)/2 = 335

    // Panel height: header(44) + rows*(btn+gap) + gap before settings + settings + bottom pad
    int panel_h = 44 + ROWS * (MBTN_H + MPAD) + 10 + (MBTN_H + MPAD) + MPAD;

    lv_obj_t *panel = lv_obj_create(qp_overlay);
    lv_obj_set_size(panel, MW, panel_h);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x161B22), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, C_BORDER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, MPAD, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t *hdr = lv_label_create(panel);
    lv_label_set_text(hdr, "Go to screen");
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hdr, C_DIM, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 6);

    // 2-column grid of screen buttons (screens 0 to NAV_CNT-1)
    for (int i = 0; i < NAV_CNT; i++) {
        int col = i % COLS;
        int row = i / COLS;
        int bx  = MPAD + col * (BTN_W + MPAD);
        int by  = 44   + row * (MBTN_H + MPAD);

        lv_obj_t *btn = lv_btn_create(panel);
        lv_obj_set_size(btn, BTN_W, MBTN_H);
        lv_obj_set_pos(btn, bx, by);

        bool active = (i == cur_screen);
        lv_obj_set_style_bg_color(btn,
            active ? lv_color_hex(0x1C3A5E) : lv_color_hex(0x0D1117), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, active ? C_ACCENT : C_BORDER, 0);
        lv_obj_set_style_border_width(btn, active ? 2 : 1, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, qp_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, SCREEN_NAMES[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, active ? C_ACCENT : C_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
    }

    // ── Settings button — full-width footer ───────────────────────────────────
    {
        int by = 44 + ROWS * (MBTN_H + MPAD) + 10;
        lv_obj_t *btn = lv_btn_create(panel);
        lv_obj_set_size(btn, MW - MPAD * 2, MBTN_H);
        lv_obj_set_pos(btn, MPAD, by);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x21262D), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, C_BORDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, qp_btn_cb, LV_EVENT_CLICKED,
                             (void*)(intptr_t)SCREEN_SETTINGS);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_SETTINGS "  Settings & System");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, C_DIM, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
    }
}

// ── Swipe + long-press gesture handler (attached to every screen) ─────────────
#define SWIPE_MIN_PX   80    // minimum horizontal travel to count as swipe
#define SWIPE_MAX_PY   120   // maximum vertical travel (reject diagonal)
#define LONGPRESS_MS   600   // hold duration to open quick-pick

static int32_t  gesture_start_x = 0;
static int32_t  gesture_start_y = 0;
static uint32_t gesture_start_t = 0;
static bool     gesture_active  = false;
static bool     gesture_fired   = false;   // prevent double-fire on same press

static void gesture_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        ui_notify_touch();  // reset inactivity timer / wake display
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        gesture_start_x = pt.x;
        gesture_start_y = pt.y;
        gesture_start_t = lv_tick_get();
        gesture_active  = true;
        gesture_fired   = false;
    }
    else if (code == LV_EVENT_PRESSING && gesture_active && !gesture_fired) {
        // Long-press detection
        if ((lv_tick_get() - gesture_start_t) >= LONGPRESS_MS) {
            gesture_fired = true;
            gesture_active = false;
            open_quick_pick();
        }
    }
    else if (code == LV_EVENT_RELEASED && gesture_active && !gesture_fired) {
        gesture_active = false;
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        int32_t dx = pt.x - gesture_start_x;
        int32_t dy = pt.y - gesture_start_y;
        if (abs(dx) >= SWIPE_MIN_PX && abs(dy) <= SWIPE_MAX_PY) {
            gesture_fired = true;
            if (dx < 0) {
                // Swipe left → next screen
                ui_show_screen((cur_screen + 1) % (SCREEN_COUNT - 1));  // wrap 0-11, settings excluded
            } else {
                // Swipe right → previous screen
                ui_show_screen((cur_screen - 1 + (SCREEN_COUNT-1)) % (SCREEN_COUNT - 1));  // wrap 0-11
            }
        }
    }
}

// ── Simple nav callbacks (still used by arrow buttons) ────────────────────────
static void nav_left_cb(lv_event_t *e) {
    ui_show_screen((cur_screen - 1 + (SCREEN_COUNT-1)) % (SCREEN_COUNT - 1));  // wrap 0-11
}
static void nav_right_cb(lv_event_t *e) {
    ui_show_screen((cur_screen + 1) % (SCREEN_COUNT - 1));  // wrap 0-11, settings excluded
}

// =============================================================================
//  SCREEN BUILDERS
// =============================================================================

// ── Shared status bar ─────────────────────────────────────────────────────────
// Each screen gets its own statusbar with its own wifi/battery labels.
// ui_update_statusbar() walks all screens and updates every instance.
//
// We keep one pointer per screen in these arrays:
static lv_obj_t *sb_wifi[SCREEN_COUNT]    = {};
static lv_obj_t *sb_bat[SCREEN_COUNT]    = {};
// Extended statusbar widgets
static lv_obj_t *sb_rssi[SCREEN_COUNT]   = {};   // RSSI value next to wifi icon
static lv_obj_t *sb_bat_pct[SCREEN_COUNT]= {};   // battery % next to battery icon
static lv_obj_t *sb_time[SCREEN_COUNT]   = {};   // HH:MM clock (centre-left)
static lv_obj_t *sb_temp[SCREEN_COUNT]   = {};   // indoor temp
static lv_obj_t *sb_hum[SCREEN_COUNT]    = {};   // indoor humidity
static lv_obj_t *sb_press[SCREEN_COUNT]  = {};   // indoor pressure

static void build_statusbar(lv_obj_t *scr, const char *title) {
    // Find which screen index this is
    int idx = -1;
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (screens[i] == scr) { idx = i; break; }
    }

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, SCREEN_W, STATUSBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x161B22), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, C_DIM, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

    // ── Left side: WiFi icon + RSSI ──────────────────────────────────────────
    lv_obj_t *wifi = lv_label_create(bar);
    lv_label_set_text(wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(wifi, C_DIM, 0);
    lv_obj_align(wifi, LV_ALIGN_LEFT_MID, PAD, 0);
    if (idx >= 0) sb_wifi[idx] = wifi;

    // RSSI value (font_14, right of wifi icon)
    lv_obj_t *rssi_lbl = lv_label_create(bar);
    lv_label_set_text(rssi_lbl, "--");
    lv_obj_set_style_text_font(rssi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rssi_lbl, C_DIM, 0);
    lv_obj_align(rssi_lbl, LV_ALIGN_LEFT_MID, PAD + 26, 0);
    if (idx >= 0) sb_rssi[idx] = rssi_lbl;

    // ── Left side: sensor values (temp · hum · press) ──────────────────────
    // Positioned after RSSI, before centre title
    lv_obj_t *sb_t = lv_label_create(bar);
    lv_label_set_text(sb_t, "--");
    lv_obj_set_style_text_font(sb_t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sb_t, C_WARM, 0);
    lv_obj_align(sb_t, LV_ALIGN_LEFT_MID, PAD + 72, 0);
    if (idx >= 0) sb_temp[idx] = sb_t;

    lv_obj_t *sb_h = lv_label_create(bar);
    lv_label_set_text(sb_h, "--");
    lv_obj_set_style_text_font(sb_h, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sb_h, C_COOL, 0);
    lv_obj_align(sb_h, LV_ALIGN_LEFT_MID, PAD + 136, 0);
    if (idx >= 0) sb_hum[idx] = sb_h;

    lv_obj_t *sb_p = lv_label_create(bar);
    lv_label_set_text(sb_p, "--");
    lv_obj_set_style_text_font(sb_p, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sb_p, C_DIM, 0);
    lv_obj_align(sb_p, LV_ALIGN_LEFT_MID, PAD + 200, 0);
    if (idx >= 0) sb_press[idx] = sb_p;

    // ── Centre-right: clock HH:MM ──────────────────────────────────────────
    // Offset right of centre so it doesn't clash with the page title
    lv_obj_t *tclk = lv_label_create(bar);
    lv_label_set_text(tclk, "--:--");
    lv_obj_set_style_text_font(tclk, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tclk, C_TEXT, 0);
    lv_obj_align(tclk, LV_ALIGN_RIGHT_MID, -(PAD + 170), 0);
    if (idx >= 0) sb_time[idx] = tclk;

    // ── Right side: battery icon + % ──────────────────────────────────────
    lv_obj_t *bat = lv_label_create(bar);
    lv_label_set_text(bat, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(bat, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bat, C_GREEN, 0);
    lv_obj_align(bat, LV_ALIGN_RIGHT_MID, -(PAD + 44), 0);
    if (idx >= 0) sb_bat[idx] = bat;

    lv_obj_t *bat_pct = lv_label_create(bar);
    lv_label_set_text(bat_pct, "--%");
    lv_obj_set_style_text_font(bat_pct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat_pct, C_GREEN, 0);
    lv_obj_align(bat_pct, LV_ALIGN_RIGHT_MID, -PAD, 0);
    if (idx >= 0) sb_bat_pct[idx] = bat_pct;
}

// ── Screen 0: Combined Clock + Indoor Sensors + Today's Weather ───────────────
//
//  NEW LAYOUT (1280 × 720):
//
//  ┌─ status bar (1280 × 44) ──────────────────────────────────────────────┐
//  ├─ NAV ─┬──── LEFT COLUMN (~700 px) ──────┬──── RIGHT PANEL (~424 px) ─┤
//  │  70   │  HH:MM:SS   (font_48)  top 55%  │  [sensor row, 62 px]       │
//  │       │  date + location pill           │  [weather card, fills rest]│
//  │       │  ─────────────────────────────  │                            │
//  │       │  Condition + description  45%   │                            │
//  ├─ NAV ─┴─────────────────────────────────┴────────────────────────────┤
//  └─ dot row ──────────────────────────────────────────────────────────────┘
// =============================================================================

#define CLK_DIV_X       770
#define CLK_RIGHT_X     (CLK_DIV_X + 16)
#define CLK_RIGHT_W     (SCREEN_W - NAV_BTN_W - CLK_RIGHT_X)

// Left column split: clock top 42%, description bottom 58%
// Moving clock higher gives the desc zone more room for the forecast paragraph
#define CLK_CONTENT_H   (SCREEN_H - STATUSBAR_H - 26)
#define CLK_SPLIT_Y     (STATUSBAR_H + (CLK_CONTENT_H * 36 / 100))

// Right panel: taller sensor cards with a visible gap before weather card
#define CLK_SENSOR_LABEL_Y  (STATUSBAR_H + 12)
#define CLK_SENSOR_CARD_Y   (CLK_SENSOR_LABEL_Y + 14)
#define CLK_SENSOR_CARD_H   80                           // tall enough for font_28 value
#define CLK_WX_CARD_Y       (CLK_SENSOR_CARD_Y + CLK_SENSOR_CARD_H + 14)  // visible gap
#define CLK_WX_CARD_H       (SCREEN_H - CLK_WX_CARD_Y - 26)

// Weather card internals — tighter hero, more grid space
#define WX_IP               12
#define WX_TEMP_Y_REL       8
#define WX_FEELS_HDR_Y_REL  10
#define WX_FEELS_VAL_Y_REL  28
#define WX_DIV1_REL         58
#define WX_GRID_TOP_REL     66


// ── Outlook icon strip dimensions ─────────────────────────────────────────────
#define OUTLOOK_SLOTS   5
#define OUTLOOK_ICON_W  80
#define OUTLOOK_ICON_H  70
#define OUTLOOK_SLOT_W  ((CLK_DIV_X - NAV_BTN_W - PAD*2) / OUTLOOK_SLOTS)

static void build_clock_screen() {
    lv_obj_t *scr = screens[SCREEN_CLOCK];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "");

    // ── VERTICAL DIVIDER ─────────────────────────────────────────────────────
    lv_obj_t *vdiv = lv_obj_create(scr);
    lv_obj_set_size(vdiv, 1, SCREEN_H - STATUSBAR_H - 20);
    lv_obj_set_pos(vdiv, CLK_DIV_X, STATUSBAR_H + 10);
    lv_obj_set_style_bg_color(vdiv, C_BORDER, 0);
    lv_obj_set_style_bg_opa(vdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(vdiv, 0, 0);
    lv_obj_set_style_pad_all(vdiv, 0, 0);
    lv_obj_clear_flag(vdiv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(vdiv, LV_OBJ_FLAG_CLICKABLE);

    // ── HORIZONTAL SPLIT LINE ─────────────────────────────────────────────────
    lv_obj_t *hdiv = lv_obj_create(scr);
    lv_obj_set_size(hdiv, CLK_DIV_X - NAV_BTN_W - PAD*2, 1);
    lv_obj_set_pos(hdiv, NAV_BTN_W + PAD, CLK_SPLIT_Y);
    lv_obj_set_style_bg_color(hdiv, C_BORDER, 0);
    lv_obj_set_style_bg_opa(hdiv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdiv, 0, 0);
    lv_obj_set_style_pad_all(hdiv, 0, 0);
    lv_obj_clear_flag(hdiv, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hdiv, LV_OBJ_FLAG_CLICKABLE);

    // =========================================================================
    //  CLOCK ZONE — top 55% of left column
    // =========================================================================
    const int CLK_ZONE_H = CLK_SPLIT_Y - STATUSBAR_H;
    const int CLK_COL_CX = NAV_BTN_W + (CLK_DIV_X - NAV_BTN_W) / 2;
    const int TIME_MID_Y = STATUSBAR_H + (CLK_ZONE_H * 40 / 100);

    // HH:MM:SS — five labels, all font_48, positioned absolutely
    // Montserrat_48 digit pair ≈ 70px, colon ≈ 18px, total ≈ 246px
    const int HH_X = CLK_COL_CX - 123;
    const int C1_X = HH_X + 70;
    const int MM_X = C1_X + 18;
    const int C2_X = MM_X + 70;
    const int SS_X = C2_X + 18;
    const int TY   = TIME_MID_Y - 29;  // top of glyph (approx half of font_48)

    clk_hh   = lv_label_create(scr);  lv_label_set_text(clk_hh,   "00");
    clk_col1 = lv_label_create(scr);  lv_label_set_text(clk_col1, ":");
    clk_mm   = lv_label_create(scr);  lv_label_set_text(clk_mm,   "00");
    clk_col2 = lv_label_create(scr);  lv_label_set_text(clk_col2, ":");
    clk_ss   = lv_label_create(scr);  lv_label_set_text(clk_ss,   "00");

    lv_obj_t *time_lbls[] = { clk_hh, clk_col1, clk_mm, clk_col2, clk_ss };
    lv_color_t time_cols[] = { C_TEXT, C_DIM, C_TEXT, C_DIM, C_DIM };
    int time_xs[]          = { HH_X, C1_X, MM_X, C2_X, SS_X };
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_text_font(time_lbls[i],  &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(time_lbls[i], time_cols[i], 0);
        lv_obj_set_pos(time_lbls[i], time_xs[i], TY);
    }

    // Date — centred in clock column, below time
    clk_date = lv_label_create(scr);
    lv_label_set_text(clk_date, "Loading...");
    lv_obj_set_style_text_font(clk_date, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(clk_date, C_TEXT, 0);
    lv_obj_set_style_text_align(clk_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(clk_date, CLK_DIV_X - NAV_BTN_W);
    lv_obj_set_pos(clk_date, NAV_BTN_W, TIME_MID_Y + 38);

    // Location + TZ pill
    const int PILL_W = 380, PILL_H = 38;
    const int PILL_X = CLK_COL_CX - PILL_W/2;
    const int PILL_Y = TIME_MID_Y + 80;

    lv_obj_t *pill = lv_obj_create(scr);
    lv_obj_set_size(pill, PILL_W, PILL_H);
    lv_obj_set_pos(pill, PILL_X, PILL_Y);
    lv_obj_set_style_bg_color(pill, C_CARD, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pill, C_BORDER, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_radius(pill, 8, 0);
    lv_obj_set_style_pad_all(pill, 0, 0);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *loc = lv_label_create(pill);
    lv_label_set_text(loc, "Sydney, NSW");
    lv_obj_set_style_text_font(loc, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(loc, C_ACCENT, 0);
    lv_obj_align(loc, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *sep = lv_label_create(pill);
    lv_label_set_text(sep, "|");
    lv_obj_set_style_text_font(sep, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sep, C_BORDER, 0);
    lv_obj_align(sep, LV_ALIGN_CENTER, 0, 0);

    clk_tz = lv_label_create(pill);
    lv_label_set_text(clk_tz, "AEDT +11");
    lv_obj_set_style_text_font(clk_tz, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(clk_tz, C_DIM, 0);
    lv_obj_align(clk_tz, LV_ALIGN_RIGHT_MID, -14, 0);

    // =========================================================================
    //  DESCRIPTION ZONE — bottom 45% of left column
    //  Condition name (font_28) + two description lines (font_20)
    //  Full column width → plenty of room, no truncation
    // =========================================================================
    const int DESC_X = NAV_BTN_W + PAD;
    const int DESC_W = CLK_DIV_X - NAV_BTN_W - PAD*2;

    clk_wx_cond = lv_label_create(scr);
    lv_label_set_text(clk_wx_cond, "—");
    lv_obj_set_style_text_font(clk_wx_cond, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(clk_wx_cond, C_ACCENT, 0);
    lv_obj_set_pos(clk_wx_cond, DESC_X, CLK_SPLIT_Y + 30);

    clk_wx_desc1 = lv_label_create(scr);
    lv_label_set_text(clk_wx_desc1, "");
    lv_obj_set_style_text_font(clk_wx_desc1, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(clk_wx_desc1, C_DIM, 0);
    lv_obj_set_width(clk_wx_desc1, DESC_W);
    lv_label_set_long_mode(clk_wx_desc1, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(clk_wx_desc1, DESC_X, CLK_SPLIT_Y + 66);

    clk_wx_desc2 = lv_label_create(scr);
    lv_label_set_text(clk_wx_desc2, "");
    lv_obj_set_style_text_font(clk_wx_desc2, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(clk_wx_desc2, C_DIM, 0);
    lv_obj_set_width(clk_wx_desc2, DESC_W);
    lv_label_set_long_mode(clk_wx_desc2, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(clk_wx_desc2, DESC_X, CLK_SPLIT_Y + 94);

    // Forecast paragraph — tomorrow + day after, natural language
    // font_20 + C_DIM to match the description lines above it
    clk_wx_forecast = lv_label_create(scr);
    lv_label_set_text(clk_wx_forecast, "");
    lv_obj_set_style_text_font(clk_wx_forecast, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(clk_wx_forecast, C_DIM, 0);
    lv_obj_set_width(clk_wx_forecast, DESC_W);
    lv_label_set_long_mode(clk_wx_forecast, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(clk_wx_forecast, DESC_X, CLK_SPLIT_Y + 126);

    // =========================================================================
    //  OUTLOOK STRIP — 5 icon slots along the bottom of the left column
    //  Slots: Today | Mon | Tue | Wed(if avail) | Moon phase
    //  Layout: icon canvas (80×70) + day label + hi/lo + rain% stacked below
    // =========================================================================
    // Strip occupies CLK_SPLIT_Y+148 → SCREEN_H-28
    const int STRIP_Y    = CLK_SPLIT_Y + 214;
    const int STRIP_BOT  = SCREEN_H - 28;
    const int STRIP_H    = STRIP_BOT - STRIP_Y;   // ≈218px
    const int SLOT_W     = (CLK_DIV_X - NAV_BTN_W - PAD*2) / OUTLOOK_SLOTS;
    const int ICON_X_OFF = (SLOT_W - OUTLOOK_ICON_W) / 2;  // centre icon in slot

    for (int i = 0; i < OUTLOOK_SLOTS; i++) {
        int sx = NAV_BTN_W + PAD + i * SLOT_W;

        // Allocate canvas buffer in PSRAM
        out_canvas_buf[i] = (lv_color_t *)heap_caps_malloc(
            OUTLOOK_ICON_W * OUTLOOK_ICON_H * sizeof(lv_color_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!out_canvas_buf[i])
            out_canvas_buf[i] = (lv_color_t *)malloc(
                OUTLOOK_ICON_W * OUTLOOK_ICON_H * sizeof(lv_color_t));

        out_canvas[i] = lv_canvas_create(scr);
        lv_canvas_set_buffer(out_canvas[i], out_canvas_buf[i],
                             OUTLOOK_ICON_W, OUTLOOK_ICON_H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(out_canvas[i], sx + ICON_X_OFF, STRIP_Y);
        lv_canvas_fill_bg(out_canvas[i], C_BG, LV_OPA_COVER);

        // Day label
        out_day[i] = lv_label_create(scr);
        lv_label_set_text(out_day[i], "---");
        lv_obj_set_style_text_font(out_day[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(out_day[i], C_DIM, 0);
        lv_obj_set_style_text_align(out_day[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(out_day[i], SLOT_W);
        lv_obj_set_pos(out_day[i], sx, STRIP_Y + OUTLOOK_ICON_H + 4);

        // Hi/Lo temp
        out_temp[i] = lv_label_create(scr);
        lv_label_set_text(out_temp[i], "--/--");
        lv_obj_set_style_text_font(out_temp[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(out_temp[i], C_TEXT, 0);
        lv_obj_set_style_text_align(out_temp[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(out_temp[i], SLOT_W);
        lv_obj_set_pos(out_temp[i], sx, STRIP_Y + OUTLOOK_ICON_H + 22);

        // Rain %
        out_rain[i] = lv_label_create(scr);
        lv_label_set_text(out_rain[i], "");
        lv_obj_set_style_text_font(out_rain[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(out_rain[i], C_COOL, 0);
        lv_obj_set_style_text_align(out_rain[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(out_rain[i], SLOT_W);
        lv_obj_set_pos(out_rain[i], sx, STRIP_Y + OUTLOOK_ICON_H + 40);
    }
    lv_obj_t *lbl_indoor = lv_label_create(scr);
    lv_label_set_text(lbl_indoor, "INDOOR | ENV PRO");
    lv_obj_set_style_text_font(lbl_indoor, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_indoor, C_DIM, 0);
    lv_obj_set_pos(lbl_indoor, CLK_RIGHT_X, CLK_SENSOR_LABEL_Y);

    // =========================================================================
    //  SENSOR ROW — 3 tight cards, refactored with helper lambda
    // =========================================================================
    const int SC_G  = 6;
    const int SC_W  = (CLK_RIGHT_W - SC_G*2) / 3;
    const int SC_IP = 10;

    auto make_sensor_card = [&](int idx, const char *hdr,
                                 lv_color_t accent) -> lv_obj_t* {
        int cx = CLK_RIGHT_X + idx*(SC_W + SC_G);
        lv_obj_t *c = make_card_nopad(scr, cx, CLK_SENSOR_CARD_Y,
                                       SC_W, CLK_SENSOR_CARD_H, accent);
        lv_obj_t *strip = lv_obj_create(c);
        lv_obj_set_size(strip, 4, CLK_SENSOR_CARD_H);
        lv_obj_set_pos(strip, 0, 0);
        lv_obj_set_style_bg_color(strip, accent, 0);
        lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(strip, 0, 0);
        lv_obj_set_style_radius(strip, 0, 0);
        lv_obj_set_style_pad_all(strip, 0, 0);
        lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(strip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *hl = lv_label_create(c);
        lv_label_set_text(hl, hdr);
        lv_obj_set_style_text_font(hl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hl, C_DIM, 0);
        lv_obj_set_pos(hl, SC_IP + 4, 8);
        return c;
    };

    lv_obj_t *sc_temp  = make_sensor_card(0, "Temperature", C_WARM );
    lv_obj_t *sc_hum   = make_sensor_card(1, "Humidity",    C_COOL );
    lv_obj_t *sc_press = make_sensor_card(2, "Pressure",    C_GREEN);

    clk_s_temp  = lv_label_create(sc_temp);
    lv_label_set_text(clk_s_temp, "--.- C");
    lv_obj_set_style_text_font(clk_s_temp, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(clk_s_temp, C_WARM, 0);
    lv_obj_set_pos(clk_s_temp, SC_IP + 4, CLK_SENSOR_CARD_H - 32);

    clk_s_hum = lv_label_create(sc_hum);
    lv_label_set_text(clk_s_hum, "--%");
    lv_obj_set_style_text_font(clk_s_hum, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(clk_s_hum, C_COOL, 0);
    lv_obj_set_pos(clk_s_hum, SC_IP + 4, CLK_SENSOR_CARD_H - 32);

    clk_s_press = lv_label_create(sc_press);
    lv_label_set_text(clk_s_press, "---- hPa");
    lv_obj_set_style_text_font(clk_s_press, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(clk_s_press, C_GREEN, 0);
    lv_obj_set_pos(clk_s_press, SC_IP + 4, CLK_SENSOR_CARD_H - 32);

    // =========================================================================
    //  WEATHER CARD — hero + 2×3 data grid
    //  No condition block here — condition lives in left-column desc zone
    // =========================================================================
    lv_obj_t *wx = make_card_nopad(scr, CLK_RIGHT_X, CLK_WX_CARD_Y,
                                    CLK_RIGHT_W, CLK_WX_CARD_H);

    // Hero: big temp left, feels-like right
    clk_wx_temp = lv_label_create(wx);
    lv_label_set_text(clk_wx_temp, "--.-C");
    lv_obj_set_style_text_font(clk_wx_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(clk_wx_temp, C_WARM, 0);
    lv_obj_set_pos(clk_wx_temp, WX_IP, WX_TEMP_Y_REL);

    lv_obj_t *fl_lbl = lv_label_create(wx);
    lv_label_set_text(fl_lbl, "Feels like");
    lv_obj_set_style_text_font(fl_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fl_lbl, C_DIM, 0);
    lv_obj_align(fl_lbl, LV_ALIGN_TOP_RIGHT, -WX_IP, WX_FEELS_HDR_Y_REL);

    clk_wx_feels = lv_label_create(wx);
    lv_label_set_text(clk_wx_feels, "--.-C");
    lv_obj_set_style_text_font(clk_wx_feels, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(clk_wx_feels, C_WARM, 0);
    lv_obj_align(clk_wx_feels, LV_ALIGN_TOP_RIGHT, -WX_IP, WX_FEELS_VAL_Y_REL);

    // Divider under hero
    lv_obj_t *d1 = lv_obj_create(wx);
    lv_obj_set_size(d1, CLK_RIGHT_W - WX_IP*2, 1);
    lv_obj_set_pos(d1, WX_IP, WX_DIV1_REL);
    lv_obj_set_style_bg_color(d1, C_BORDER, 0);
    lv_obj_set_style_bg_opa(d1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d1, 0, 0);
    lv_obj_set_style_pad_all(d1, 0, 0);
    lv_obj_clear_flag(d1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(d1, LV_OBJ_FLAG_CLICKABLE);

    // ── 2×3 data grid ────────────────────────────────────────────────────────
    // card_H ≈ 538px, grid from 76px → 520px = 444px / 3 = 148px per row
    // value at fixed ry+34 (header at ry+10, value below it)
    const int GRD_H   = CLK_WX_CARD_H - WX_GRID_TOP_REL - 18;
    const int ROW_H   = GRD_H / 3;
    const int COL_W   = (CLK_RIGHT_W - WX_IP*2 - 1) / 2;
    const int COL_B_X = WX_IP + COL_W + 1;

    lv_obj_t *vd = lv_obj_create(wx);
    lv_obj_set_size(vd, 1, GRD_H);
    lv_obj_set_pos(vd, WX_IP + COL_W, WX_GRID_TOP_REL);
    lv_obj_set_style_bg_color(vd, C_BORDER, 0);
    lv_obj_set_style_bg_opa(vd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(vd, 0, 0);
    lv_obj_set_style_pad_all(vd, 0, 0);
    lv_obj_clear_flag(vd, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(vd, LV_OBJ_FLAG_CLICKABLE);

    // Cell builder — no background rect, just header + value directly on card
    auto make_cell = [&](int col_x, int row, const char *hdr,
                          lv_color_t val_col) -> lv_obj_t* {
        int ry = WX_GRID_TOP_REL + row * ROW_H;
        if (row > 0) {
            lv_obj_t *rd = lv_obj_create(wx);
            lv_obj_set_size(rd, CLK_RIGHT_W - WX_IP*2, 1);
            lv_obj_set_pos(rd, WX_IP, ry);
            lv_obj_set_style_bg_color(rd, C_BORDER, 0);
            lv_obj_set_style_bg_opa(rd, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(rd, 0, 0);
            lv_obj_set_style_pad_all(rd, 0, 0);
            lv_obj_clear_flag(rd, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(rd, LV_OBJ_FLAG_CLICKABLE);
        }
        lv_obj_t *hl = lv_label_create(wx);
        lv_label_set_text(hl, hdr);
        lv_obj_set_style_text_font(hl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hl, C_DIM, 0);
        lv_obj_set_pos(hl, col_x + 8, ry + 10);
        lv_obj_t *vl = lv_label_create(wx);
        lv_label_set_text(vl, "--");
        lv_obj_set_style_text_font(vl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(vl, val_col, 0);
        lv_obj_set_pos(vl, col_x + 8, ry + 28);
        return vl;
    };

    clk_wx_wind  = make_cell(WX_IP,    0, "Wind",            C_TEXT  );
    clk_wx_uv    = make_cell(WX_IP,    1, "UV Index",        C_YELLOW);
    clk_wx_rain  = make_cell(WX_IP,    2, "Precipitation",   C_COOL  );
    clk_wx_hum   = make_cell(COL_B_X,  0, "Humidity",        C_COOL  );
    clk_wx_vis   = make_cell(COL_B_X,  1, "Visibility",      C_TEXT  );

    // Sunrise / Sunset — two lines in the last cell (no truncation)
    {
        int ry = WX_GRID_TOP_REL + 2 * ROW_H;
        lv_obj_t *rd = lv_obj_create(wx);
        lv_obj_set_size(rd, CLK_RIGHT_W - WX_IP*2, 1);
        lv_obj_set_pos(rd, WX_IP, ry);
        lv_obj_set_style_bg_color(rd, C_BORDER, 0);
        lv_obj_set_style_bg_opa(rd, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rd, 0, 0);
        lv_obj_set_style_pad_all(rd, 0, 0);
        lv_obj_clear_flag(rd, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(rd, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *sh = lv_label_create(wx);
        lv_label_set_text(sh, "Sunrise / Sunset");
        lv_obj_set_style_text_font(sh, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sh, C_DIM, 0);
        lv_obj_set_pos(sh, COL_B_X + 8, ry + 10);

        // Two stacked font_28 lines — rise on top, set below
        clk_wx_sun = lv_label_create(wx);
        lv_label_set_text(clk_wx_sun, "--:-- AM\n--:-- PM");
        lv_obj_set_style_text_font(clk_wx_sun, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(clk_wx_sun, C_YELLOW, 0);
        lv_obj_set_pos(clk_wx_sun, COL_B_X + 8, ry + 32);
    }

    clk_wx_updated = lv_label_create(wx);
    lv_label_set_text(clk_wx_updated, "");
    lv_obj_set_style_text_font(clk_wx_updated, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(clk_wx_updated, C_BORDER, 0);
    lv_obj_align(clk_wx_updated, LV_ALIGN_BOTTOM_RIGHT, -WX_IP, -6);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}
    // ── Screen 1: Local Conditions (BME688) ──────────────────────────────────────
static void build_local_screen() {
    lv_obj_t *scr = screens[SCREEN_LOCAL];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Indoor Conditions - ENV Pro (BME688)");

    // ── Layout constants ──────────────────────────────────────────────────────
    const int GAP     = 12;
    const int LEFT_W  = 230;
    const int LEFT_X  = NAV_BTN_W + PAD;
    const int RIGHT_X = LEFT_X + LEFT_W + GAP;
    const int RIGHT_W = SCREEN_W - RIGHT_X - NAV_BTN_W - PAD;
    const int TOP_Y   = CONTENT_Y + PAD;
    const int COL_H   = CONTENT_H - PAD * 2;
    const int STAT_H  = (COL_H - GAP * 2) / 3;

    // ── LEFT COLUMN — 3 large stat cards ─────────────────────────────────────
    // Temperature
    {
        lv_obj_t *c = make_card(scr, LEFT_X, TOP_Y, LEFT_W, STAT_H);
        make_label(c, "Temperature", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);
        loc_temp     = make_label(c, "--.-\xC2\xB0""C",
                                  &lv_font_montserrat_48, C_WARM, LV_ALIGN_TOP_LEFT, 0, 22);
        loc_temp_sub = make_label(c, "Indoor",
                                  &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    // Humidity
    {
        lv_obj_t *c = make_card(scr, LEFT_X, TOP_Y + STAT_H + GAP, LEFT_W, STAT_H);
        make_label(c, "Humidity", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);
        loc_hum     = make_label(c, "--%",
                                 &lv_font_montserrat_48, C_COOL, LV_ALIGN_TOP_LEFT, 0, 22);
        loc_hum_sub = make_label(c, "--",
                                 &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }
    // Pressure + trend
    {
        lv_obj_t *c = make_card(scr, LEFT_X, TOP_Y + (STAT_H + GAP) * 2, LEFT_W, STAT_H);
        make_label(c, "Pressure", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);
        loc_press = make_label(c, "---- hPa",
                               &lv_font_montserrat_28, C_TEXT, LV_ALIGN_TOP_LEFT, 0, 22);
        loc_trend = make_label(c, "--",
                               &lv_font_montserrat_16, C_GREEN, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // ── RIGHT TOP — IAQ panel ────────────────────────────────────────────────
    const int IAQ_CARD_H  = (int)(COL_H * 0.60f);
    const int COMF_CARD_H = COL_H - IAQ_CARD_H - GAP;

    lv_obj_t *iaq_card = make_card(scr, RIGHT_X, TOP_Y, RIGHT_W, IAQ_CARD_H);

    // Header: title left, scale right
    make_label(iaq_card, "Air quality index (IAQ)",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);
    loc_iaq_scale_lbl = make_label(iaq_card, "Scale 0 - 500",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Score number
    loc_iaq = make_label(iaq_card, "--",
                         &lv_font_montserrat_48, C_GREEN, LV_ALIGN_TOP_LEFT, 0, 24);

    // Quality label
    loc_iaq_label = make_label(iaq_card, "--",
                               &lv_font_montserrat_20, C_GREEN, LV_ALIGN_TOP_LEFT, 0, 90);

    // Accuracy badge pill
    loc_iaq_acc_badge = make_label(iaq_card, "--",
                                   &lv_font_montserrat_14, C_BG, LV_ALIGN_TOP_LEFT, 0, 120);
    lv_obj_set_style_bg_color(loc_iaq_acc_badge, C_DIM, 0);
    lv_obj_set_style_bg_opa(loc_iaq_acc_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(loc_iaq_acc_badge, 4, 0);
    lv_obj_set_style_pad_hor(loc_iaq_acc_badge, 8, 0);
    lv_obj_set_style_pad_ver(loc_iaq_acc_badge, 3, 0);

    // Layout constants for bar and legend
    const int BAR_W   = RIGHT_W - PAD * 2;
    const int LEG_Y   = 156;
    const int BAR_Y   = 178;
    const int BAR_H2  = 10;
    const int SCALE_Y = BAR_Y + BAR_H2 + 6;

    // Legend row
    {
        struct { uint32_t col; const char *lbl; } legend[] = {
            { 0x3FB950, "0-50 Excellent" },
            { 0xD29922, "100-200 Moderate" },
            { 0xA050D0, "200-300 Heavily polluted" },
            { 0xF85149, "300+ Severely polluted" },
        };
        int lx = 0;
        for (auto &le : legend) {
            lv_obj_t *dot = lv_obj_create(iaq_card);
            lv_obj_set_size(dot, 8, 8);
            lv_obj_set_pos(dot, lx, LEG_Y + 3);
            lv_obj_set_style_bg_color(dot, lv_color_hex(le.col), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_set_style_radius(dot, 2, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lx += 12;
            lv_obj_t *dl = lv_label_create(iaq_card);
            lv_label_set_text(dl, le.lbl);
            lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(dl, lv_color_hex(le.col), 0);
            lv_obj_set_pos(dl, lx, LEG_Y);
            lx += strlen(le.lbl) * 8 + 16;
        }
    }

    // Bar track
    lv_obj_t *bar_track = lv_obj_create(iaq_card);
    lv_obj_set_size(bar_track, BAR_W, BAR_H2);
    lv_obj_set_pos(bar_track, 0, BAR_Y);
    lv_obj_set_style_bg_color(bar_track, C_BORDER, 0);
    lv_obj_set_style_bg_opa(bar_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_track, 0, 0);
    lv_obj_set_style_radius(bar_track, 5, 0);
    lv_obj_set_style_pad_all(bar_track, 0, 0);
    lv_obj_clear_flag(bar_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Bar fill (updated at runtime)
    loc_iaq_bar_fill = lv_obj_create(iaq_card);
    lv_obj_set_size(loc_iaq_bar_fill, 0, BAR_H2);
    lv_obj_set_pos(loc_iaq_bar_fill, 0, BAR_Y);
    lv_obj_set_style_bg_color(loc_iaq_bar_fill, C_GREEN, 0);
    lv_obj_set_style_bg_opa(loc_iaq_bar_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(loc_iaq_bar_fill, 0, 0);
    lv_obj_set_style_radius(loc_iaq_bar_fill, 5, 0);
    lv_obj_set_style_pad_all(loc_iaq_bar_fill, 0, 0);
    lv_obj_clear_flag(loc_iaq_bar_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Scale labels
    const char *scale_lbls[] = {"0","50","100","150","200","250","300","500"};
    const int   scale_vals[] = {  0,  50,  100,  150,  200,  250,  300,  500};
    for (int i = 0; i < 8; i++) {
        int px = (int)((scale_vals[i] / 500.0f) * BAR_W);
        lv_obj_t *sl = lv_label_create(iaq_card);
        lv_label_set_text(sl, scale_lbls[i]);
        lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sl, C_DIM, 0);
        lv_obj_set_pos(sl, px, SCALE_Y);
    }

    // Debug row: gas resistance | baseline | heater spec
    const int DEBUG_Y = IAQ_CARD_H - PAD * 2 - 18;
    loc_iaq_resist = make_label(iaq_card, "Gas resistance: -- ohm",
                                &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, DEBUG_Y);
    loc_iaq_base   = make_label(iaq_card, "Baseline: -- ohm",
                                &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_MID, 0, DEBUG_Y);
    make_label(iaq_card, "Heater: 320Â°""C / 150ms",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_RIGHT, 0, DEBUG_Y);

    // Nullify old chart statics — canvas removed from this screen
    loc_chart_canvas = nullptr;
    loc_chart_buf    = nullptr;
    loc_wait_lbl     = nullptr;

    // ── RIGHT BOTTOM — comfort panel ──────────────────────────────────────────
    lv_obj_t *comf_card = make_card(scr, RIGHT_X, TOP_Y + IAQ_CARD_H + GAP,
                                    RIGHT_W, COMF_CARD_H);

    const int CC = (RIGHT_W - PAD * 2) / 3;   // column width inside comfort card
    const int BAR_H = 6;

    // Helper lambda — builds one comfort column
    // Returns the value label pointer
    struct ComfCol { lv_obj_t *val; lv_obj_t *bar_fill; };
    auto make_comf_col = [&](int col_idx, const char *header, lv_color_t val_col) -> ComfCol {
        int cx = col_idx * CC;
        make_label(comf_card, header, &lv_font_montserrat_14, C_DIM,
                   LV_ALIGN_TOP_LEFT, cx, 0);
        lv_obj_t *val = make_label(comf_card, "--",
                                   &lv_font_montserrat_28, val_col,
                                   LV_ALIGN_TOP_LEFT, cx, 20);
        // Bar track
        lv_obj_t *track = lv_obj_create(comf_card);
        lv_obj_set_size(track, CC - 8, BAR_H);
        lv_obj_set_pos(track, cx, COMF_CARD_H - PAD * 2 - BAR_H);
        lv_obj_set_style_bg_color(track, C_BORDER, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_radius(track, 3, 0);
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        // Bar fill — starts at zero width, updated at runtime
        lv_obj_t *fill = lv_obj_create(comf_card);
        lv_obj_set_size(fill, 0, BAR_H);
        lv_obj_set_pos(fill, cx, COMF_CARD_H - PAD * 2 - BAR_H);
        lv_obj_set_style_bg_color(fill, val_col, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, 3, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        return {val, fill};
    };

    auto dp = make_comf_col(0, "Dew point",   C_COOL);
    auto hi = make_comf_col(1, "Heat index",  C_WARM);
    auto cf = make_comf_col(2, "Comfort",     C_GREEN);
    loc_dewpoint = dp.val;  loc_dewbar  = dp.bar_fill;
    loc_heatidx  = hi.val;  loc_heatbar = hi.bar_fill;
    loc_comfort  = cf.val;  loc_comfbar = cf.bar_fill;

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// ── Screen 2: Weather Current ─────────────────────────────────────────────────
//
//  Layout (1280 × 676 content area):
//
//  ┌────────────────────────────────────────┬──────────────────────────────┐
//  │  HERO card  (left, full height)        │  UV card  (top right)        │
//  │  ─ temp, feels, condition, desc        │  ─ big number, bar, advice   │
//  │  ─ hourly strip (flex-fills gap)       ├──────────────────────────────┤
//  │  ─ 3×2 mini stats pinned to bottom     │  Detail card (bottom right)  │
//  │                                        │  ─ 3×2: sun/moon/season/event│
//  └────────────────────────────────────────┴──────────────────────────────┘
//
static void build_weather_screen() {
    lv_obj_t *scr = screens[SCREEN_WEATHER];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Current Weather - " WEATHER_LOCATION);

    // ── Layout constants ──────────────────────────────────────────────────────
    const int GAP      = 12;
    const int LEFT_W   = 740;
    const int LEFT_X   = NAV_BTN_W + PAD;
    const int RIGHT_X  = LEFT_X + LEFT_W + GAP;
    const int RIGHT_W  = SCREEN_W - RIGHT_X - NAV_BTN_W - PAD;
    const int TOP_Y    = CONTENT_Y + PAD;
    const int HERO_H   = CONTENT_H - PAD * 2;
    const int HALF_H   = (HERO_H - GAP) / 2;

    // ── HERO card ─────────────────────────────────────────────────────────────
    lv_obj_t *hero = make_card_nopad(scr, LEFT_X, TOP_Y, LEFT_W, HERO_H);
    lv_obj_set_style_pad_all(hero, PAD, 0);

    // Weather icon canvas — top-right inside hero
    wx_icon_buf = (lv_color_t *)heap_caps_malloc(
        OUTLOOK_ICON_W * OUTLOOK_ICON_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wx_icon_buf) wx_icon_buf = (lv_color_t *)malloc(
        OUTLOOK_ICON_W * OUTLOOK_ICON_H * sizeof(lv_color_t));
    wx_icon_canvas = lv_canvas_create(hero);
    lv_canvas_set_buffer(wx_icon_canvas, wx_icon_buf,
                         OUTLOOK_ICON_W, OUTLOOK_ICON_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(wx_icon_canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_fill_bg(wx_icon_canvas, C_CARD, LV_OPA_COVER);

    // Temp block — big number top-left
    make_label(hero, "Temperature", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);
    wx_temp  = make_label(hero, "-- \xC2\xB0""C",
                          &lv_font_montserrat_48, C_WARM, LV_ALIGN_TOP_LEFT, 0, 20);

    // Feels-like + humidity — in the middle zone between temp and the weather icon
    // font_48 temp ~200px wide; icon 80px anchored top-right; ~400px gap between them
    wx_feels    = make_label(hero, "Feels like --\xC2\xB0",
                             &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_LEFT, 215, 28);
    make_label(hero, "Humidity", &lv_font_montserrat_14, C_DIM,  LV_ALIGN_TOP_LEFT, 215, 60);
    wx_hum_hero = make_label(hero, "--%",
                             &lv_font_montserrat_20, C_COOL, LV_ALIGN_TOP_LEFT, 215, 76);

    // Condition — full width below temp
    wx_cond = make_label(hero, "--",
                         &lv_font_montserrat_28, C_ACCENT, LV_ALIGN_TOP_LEFT, 0, 96);

    // Divider 1 — between condition and hourly strip, pushed down to give cond more room
    const int DIV1_Y = 148;
    lv_obj_t *d1 = lv_obj_create(hero);
    lv_obj_set_size(d1, LEFT_W - PAD * 2, 1);
    lv_obj_set_pos(d1, 0, DIV1_Y);
    lv_obj_set_style_bg_color(d1, C_BORDER, 0);
    lv_obj_set_style_bg_opa(d1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d1, 0, 0);
    lv_obj_set_style_pad_all(d1, 0, 0);
    lv_obj_clear_flag(d1, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // "Next 6 hours" label
    make_label(hero, "Next 6 hours", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, 0, DIV1_Y + 10);

    // ── Hourly slot cards ─────────────────────────────────────────────────────
    // They span from below the label to above the mini-grid divider.
    // Mini-grid is 2 rows × ~22px value + ~16px label + ~14px sub = ~104px + gap.
    // We'll pin them at HERO_H - PAD - MINI_GRID_H. Mini grid = 2 rows of 52px = 104 + 8 gap.
    const int MINI_H    = 112;
    const int DIV2_Y    = HERO_H - PAD * 2 - MINI_H - 8;  // divider above mini grid
    const int STRIP_TOP = DIV1_Y + 32;
    // Reserve WX_SPARK_H + 4px gap below cards for the sparkline
    const int STRIP_H   = DIV2_Y - STRIP_TOP - 8 - WX_SPARK_H - 4;
    const int SLOT_W    = (LEFT_W - PAD * 2 - 5 * GAP) / WX_HOURLY_SLOTS;

    for (int i = 0; i < WX_HOURLY_SLOTS; i++) {
        int sx = i * (SLOT_W + GAP);
        wx_h_card[i] = make_card_nopad(hero, sx, STRIP_TOP, SLOT_W, STRIP_H, C_BORDER);
        lv_obj_set_style_bg_color(wx_h_card[i], lv_color_hex(0x0D1117), 0);

        // Icon canvas inside slot
        wx_h_buf[i] = (lv_color_t *)heap_caps_malloc(
            WX_HOURLY_ICON_W * WX_HOURLY_ICON_H * sizeof(lv_color_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!wx_h_buf[i]) wx_h_buf[i] = (lv_color_t *)malloc(
            WX_HOURLY_ICON_W * WX_HOURLY_ICON_H * sizeof(lv_color_t));
        wx_h_canvas[i] = lv_canvas_create(wx_h_card[i]);
        lv_canvas_set_buffer(wx_h_canvas[i], wx_h_buf[i],
                             WX_HOURLY_ICON_W, WX_HOURLY_ICON_H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_align(wx_h_canvas[i], LV_ALIGN_CENTER, 0, -8);
        lv_canvas_fill_bg(wx_h_canvas[i], lv_color_hex(0x0D1117), LV_OPA_COVER);

        // Time label — top
        wx_h_time[i] = make_label(wx_h_card[i], "--",
                                  &lv_font_montserrat_14, C_DIM,
                                  LV_ALIGN_TOP_MID, 0, 6);
        // Temp — below icon
        wx_h_temp[i] = make_label(wx_h_card[i], "--\xC2\xB0",
                                  &lv_font_montserrat_16, C_TEXT,
                                  LV_ALIGN_BOTTOM_MID, 0, -18);
        // Rain % — bottom
        wx_h_rain[i] = make_label(wx_h_card[i], "",
                                  &lv_font_montserrat_14, C_DIM,
                                  LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    // ── Sparkline canvas — spans full strip width below the slot cards ─────────
    wx_spark_w = LEFT_W - PAD * 2;
    const int SPARK_Y = STRIP_TOP + STRIP_H + 4;
    wx_spark_buf = (lv_color_t *)heap_caps_malloc(
        wx_spark_w * WX_SPARK_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wx_spark_buf) wx_spark_buf = (lv_color_t *)malloc(
        wx_spark_w * WX_SPARK_H * sizeof(lv_color_t));
    wx_spark_canvas = lv_canvas_create(hero);
    lv_canvas_set_buffer(wx_spark_canvas, wx_spark_buf,
                         wx_spark_w, WX_SPARK_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(wx_spark_canvas, 0, SPARK_Y);
    lv_canvas_fill_bg(wx_spark_canvas, lv_color_hex(0x0D1117), LV_OPA_COVER);

    // Divider 2 — above mini grid
    lv_obj_t *d2 = lv_obj_create(hero);
    lv_obj_set_size(d2, LEFT_W - PAD * 2, 1);
    lv_obj_set_pos(d2, 0, DIV2_Y);
    lv_obj_set_style_bg_color(d2, C_BORDER, 0);
    lv_obj_set_style_bg_opa(d2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(d2, 0, 0);
    lv_obj_set_style_pad_all(d2, 0, 0);
    lv_obj_clear_flag(d2, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Mini stats grid (3×2) pinned to bottom of hero ────────────────────────
    // Row 1: Wind | Pressure | Visibility
    // Row 2: Precip | Dew point | Cloud cover
    const int MG_Y    = DIV2_Y + 8;
    const int MG_COL  = (LEFT_W - PAD * 2) / 3;
    const int ROW_H   = 52;

    // Row 1
    {
        lv_obj_t *c = make_card_nopad(hero, 0,          MG_Y, MG_COL - 4, ROW_H);
        make_label(c, "Wind",     &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 8, 4);
        wx_wind      = make_label(c, "-- km/h --",
                                  &lv_font_montserrat_16, C_TEXT, LV_ALIGN_TOP_LEFT, 8, 20);
        wx_wind_gust = make_label(c, "Gust -- km/h",
                                  &lv_font_montserrat_14, C_DIM,  LV_ALIGN_TOP_LEFT, 8, 36);
    }
    {
        lv_obj_t *c = make_card_nopad(hero, MG_COL,     MG_Y, MG_COL - 4, ROW_H);
        make_label(c, "Pressure", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 8, 4);
        wx_pressure = make_label(c, "-- hPa",
                                 &lv_font_montserrat_16, C_TEXT, LV_ALIGN_TOP_LEFT, 8, 20);
        wx_pressure_trend = make_label(c, "--",
                                       &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 8, 36);
    }
    {
        lv_obj_t *c = make_card_nopad(hero, MG_COL * 2, MG_Y, MG_COL - 4, ROW_H);
        make_label(c, "Visibility",&lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 8, 4);
        wx_vis = make_label(c, "-- km",
                            &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_LEFT, 8, 22);
    }

    // Row 2
    {
        lv_obj_t *c = make_card_nopad(hero, 0,          MG_Y + ROW_H + 4, MG_COL - 4, ROW_H);
        make_label(c, "Precip today", &lv_font_montserrat_14, C_DIM,  LV_ALIGN_TOP_LEFT, 8, 4);
        wx_precip = make_label(c, "-- mm",
                               &lv_font_montserrat_16, C_COOL, LV_ALIGN_TOP_LEFT, 8, 20);
        wx_hum    = make_label(c, "--% rain chance",
                               &lv_font_montserrat_14, C_DIM,  LV_ALIGN_TOP_LEFT, 8, 36);
    }
    {
        lv_obj_t *c = make_card_nopad(hero, MG_COL,     MG_Y + ROW_H + 4, MG_COL - 4, ROW_H);
        make_label(c, "Dew point", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 8, 4);
        wx_dewpoint = make_label(c, "--\xC2\xB0""C",
                                 &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_LEFT, 8, 22);
    }
    {
        lv_obj_t *c = make_card_nopad(hero, MG_COL * 2, MG_Y + ROW_H + 4, MG_COL - 4, ROW_H);
        make_label(c, "Cloud cover", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 8, 4);
        wx_cloud = make_label(c, "--%",
                              &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_LEFT, 8, 22);
    }

    // Last updated — bottom of screen
    wx_updated = make_label(scr, "Not yet fetched", &lv_font_montserrat_14, C_BORDER,
                            LV_ALIGN_BOTTOM_MID, 0, -6);

    // ── UV + Air Quality card (top right) ────────────────────────────────────
    // Layout (card interior, after make_card PAD offset):
    // y=  0  UV Index label
    // y= 18  Big UV number (font_48) + category beside it
    // y= 76  UV colour bar
    // y= 90  Peak time    |    SPF advice  (same row, left/right)
    // y=108  Divider
    // y=116  AIR QUALITY header
    // y=134  Category (font_20) + EPA index
    // y=158  PM2.5 / PM10 / O3 / NO2 rows
    lv_obj_t *uv_card = make_card(scr, RIGHT_X, TOP_Y, RIGHT_W, HALF_H);

    // UV section header
    make_label(uv_card, "UV Index", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    // Big UV number + category
    wx_uv_num = make_label(uv_card, "—",
                           &lv_font_montserrat_48, C_YELLOW, LV_ALIGN_TOP_LEFT, 0, 18);
    wx_uv_cat = make_label(uv_card, "—",
                           &lv_font_montserrat_16, C_YELLOW, LV_ALIGN_TOP_LEFT, 76, 38);

    // UV colour bar — moved up to y=76
    const int BAR_Y = 76, BAR_H = 8, BAR_W = RIGHT_W - PAD * 2;
    wx_uv_bar_w = BAR_W;
    struct { lv_color_t col; float frac; } segs[] = {
        { lv_color_hex(0x3FB950), 3.0f/11.0f },   // green  — Low    (0-3)
        { lv_color_hex(0xD29922), 3.0f/11.0f },   // yellow — Moderate (3-6)
        { lv_color_hex(0xFF7B50), 2.0f/11.0f },   // orange — High   (6-8)
        { lv_color_hex(0xF85149), 2.0f/11.0f },   // red    — Very High (8-10)
        { lv_color_hex(0x9B59B6), 1.0f/11.0f },   // purple — Extreme (10-11)
    };
    int seg_x = 0;
    for (int si = 0; si < 5; si++) {
        int seg_w = (int)(segs[si].frac * BAR_W);
        if (si == 4) seg_w = BAR_W - seg_x;
        lv_obj_t *seg = lv_obj_create(uv_card);
        lv_obj_set_size(seg, seg_w, BAR_H);
        lv_obj_set_pos(seg, seg_x, BAR_Y);
        lv_obj_set_style_bg_color(seg, segs[si].col, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_radius(seg, si == 0 ? 4 : (si == 4 ? 4 : 0), 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        seg_x += seg_w;
    }

    // Marker dot on the bar
    wx_uv_marker = lv_obj_create(uv_card);
    lv_obj_set_size(wx_uv_marker, 14, 14);
    lv_obj_set_style_bg_color(wx_uv_marker, C_TEXT, 0);
    lv_obj_set_style_bg_opa(wx_uv_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(wx_uv_marker, C_BG, 0);
    lv_obj_set_style_border_width(wx_uv_marker, 2, 0);
    lv_obj_set_style_radius(wx_uv_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(wx_uv_marker, 0, 0);
    lv_obj_set_pos(wx_uv_marker, 0, BAR_Y - 3);
    lv_obj_clear_flag(wx_uv_marker, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // SPF advice on its own line (most important), peak below
    wx_uv_advice = make_label(uv_card, "—",
                              &lv_font_montserrat_14, C_YELLOW, LV_ALIGN_TOP_LEFT, 0, 90);
    wx_uv_peak   = make_label(uv_card, "Peak: —",
                              &lv_font_montserrat_14, C_DIM,    LV_ALIGN_TOP_LEFT, 0, 106);

    // ── Divider ───────────────────────────────────────────────────────────────
    {
        lv_obj_t *dv = lv_obj_create(uv_card);
        lv_obj_set_size(dv, BAR_W, 1);
        lv_obj_set_pos(dv, 0, 122);
        lv_obj_set_style_bg_color(dv, C_BORDER, 0);
        lv_obj_set_style_bg_opa(dv, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dv, 0, 0);
        lv_obj_set_style_pad_all(dv, 0, 0);
        lv_obj_clear_flag(dv, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    // ── Air Quality section ───────────────────────────────────────────────────
    make_label(uv_card, "Air Quality", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 130);

    // Category + EPA index on same line
    wx_aqi_cat = make_label(uv_card, "--",
                            &lv_font_montserrat_20, C_GREEN, LV_ALIGN_TOP_LEFT,  0, 148);
    wx_aqi_epa = make_label(uv_card, "EPA --/6",
                            &lv_font_montserrat_14, C_DIM,   LV_ALIGN_TOP_RIGHT, 0, 154);

    // Pollutant rows — label on left, value on right
    // Helper lambda: creates a left dim label + right value label at given y
    const int VW = BAR_W / 2;  // value label width
    auto aqi_row = [&](const char *lbl, lv_obj_t **val_ptr, int y) {
        make_label(uv_card, lbl, &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, y);
        lv_obj_t *vl = make_label(uv_card, "--", &lv_font_montserrat_14,
                                  C_DIM, LV_ALIGN_TOP_RIGHT, 0, y);
        lv_obj_set_width(vl, VW);
        lv_label_set_long_mode(vl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(vl, LV_TEXT_ALIGN_RIGHT, 0);
        if (val_ptr) *val_ptr = vl;
    };

    aqi_row("PM2.5 ug/m3", &wx_aqi_pm25, 172);
    aqi_row("PM10  ug/m3", &wx_aqi_pm10, 190);
    aqi_row("O3    ug/m3", &wx_aqi_o3,   208);
    aqi_row("NO2   ug/m3", &wx_aqi_no2,  226);

    // ── Detail card (bottom right) ────────────────────────────────────────────
    lv_obj_t *det = make_card(scr, RIGHT_X, TOP_Y + HALF_H + GAP, RIGHT_W, HALF_H);
    // DC based on interior width (card has PAD on each side)
    const int INNER_W = RIGHT_W - PAD * 2;
    const int DC      = INNER_W / 3;
    const int DR      = HALF_H / 2 - PAD;

    // Row 1 — Sunrise | Solar noon | Sunset
    make_label(det, "Sunrise",    &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0,        0);
    make_label(det, "Solar noon", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, DC,       0);
    make_label(det, "Sunset",     &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, DC * 2,   0);
    wx_sunrise    = make_label(det, "--",&lv_font_montserrat_16,lv_color_hex(0xD29922),LV_ALIGN_TOP_LEFT,0,       18);
    wx_solarnoon  = make_label(det, "--",&lv_font_montserrat_16,lv_color_hex(0xFFD033),LV_ALIGN_TOP_LEFT,DC,      18);
    wx_sunset     = make_label(det, "--",&lv_font_montserrat_16,C_WARM,                LV_ALIGN_TOP_LEFT,DC * 2,  18);
    wx_sunrise_sub   = make_label(det,"--",&lv_font_montserrat_14,C_DIM,LV_ALIGN_TOP_LEFT,0,       38);
    wx_solarnoon_sub = make_label(det,"--",&lv_font_montserrat_14,C_DIM,LV_ALIGN_TOP_LEFT,DC,      38);
    wx_sunset_sub    = make_label(det,"--",&lv_font_montserrat_14,C_DIM,LV_ALIGN_TOP_LEFT,DC * 2,  38);
    // Clip each label to its column width so text never bleeds into the next column
    const int COL_MAX = DC - 4;
    lv_obj_set_width(wx_sunrise,      COL_MAX); lv_label_set_long_mode(wx_sunrise,      LV_LABEL_LONG_CLIP);
    lv_obj_set_width(wx_solarnoon,    COL_MAX); lv_label_set_long_mode(wx_solarnoon,    LV_LABEL_LONG_CLIP);
    lv_obj_set_width(wx_sunset,       COL_MAX); lv_label_set_long_mode(wx_sunset,       LV_LABEL_LONG_CLIP);
    lv_obj_set_width(wx_sunrise_sub,  COL_MAX); lv_label_set_long_mode(wx_sunrise_sub,  LV_LABEL_LONG_CLIP);
    lv_obj_set_width(wx_solarnoon_sub,COL_MAX); lv_label_set_long_mode(wx_solarnoon_sub,LV_LABEL_LONG_CLIP);
    lv_obj_set_width(wx_sunset_sub,   COL_MAX); lv_label_set_long_mode(wx_sunset_sub,   LV_LABEL_LONG_CLIP);

    // Row divider
    lv_obj_t *dd = lv_obj_create(det);
    lv_obj_set_size(dd, INNER_W, 1);
    lv_obj_set_pos(dd, 0, DR);
    lv_obj_set_style_bg_color(dd, C_BORDER, 0);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_pad_all(dd, 0, 0);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Row 2 — 3 stacked lines: Moon phase / Next event / Season
    // Each line: dim label on left, coloured value on right
    const int R2_BASE = DR + 10;
    const int R2_STEP = (HALF_H - PAD - R2_BASE) / 3;  // divide remaining space evenly

    make_label(det, "Moon phase", &lv_font_montserrat_14, C_DIM,   LV_ALIGN_TOP_LEFT,  0, R2_BASE);
    wx_moon_phase = make_label(det, "--", &lv_font_montserrat_16, C_ACCENT, LV_ALIGN_TOP_RIGHT, 0, R2_BASE);

    make_label(det, "Next event", &lv_font_montserrat_14, C_DIM,   LV_ALIGN_TOP_LEFT,  0, R2_BASE + R2_STEP);
    wx_next_event     = make_label(det, "--", &lv_font_montserrat_16, C_GREEN, LV_ALIGN_TOP_RIGHT, 0, R2_BASE + R2_STEP);
    wx_next_event_sub = make_label(det, "--", &lv_font_montserrat_14, C_DIM,   LV_ALIGN_TOP_RIGHT, 0, R2_BASE + R2_STEP + 18);

    make_label(det, "Season",     &lv_font_montserrat_14, C_DIM,   LV_ALIGN_TOP_LEFT,  0, R2_BASE + R2_STEP * 2);
    wx_season = make_label(det, "--", &lv_font_montserrat_16, C_WARM,   LV_ALIGN_TOP_RIGHT, 0, R2_BASE + R2_STEP * 2);

    // Moon sub-label (illumination) — sits under moon phase line on right
    wx_moon_sub = make_label(det, "--", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_RIGHT, 0, R2_BASE + 18);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// ── Screen 3: Forecast ────────────────────────────────────────────────────────
static void build_forecast_screen() {
    lv_obj_t *scr = screens[SCREEN_FORECAST];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "3-Day Forecast - " WEATHER_LOCATION);

    const int cy = CONTENT_Y + PAD;
    const int cw = (SCREEN_W - NAV_BTN_W * 2 - PAD * 4) / 3;
    const int ch = CONTENT_H - PAD * 2;

    for (int i = 0; i < 3; i++) {
        int cx = NAV_BTN_W + PAD + i * (cw + PAD);
        fc_cards[i] = make_card(scr, cx, cy, cw, ch);
        lv_obj_t *l = lv_label_create(fc_cards[i]);
        lv_label_set_text(l, "--");
        lv_obj_set_style_text_color(l, C_DIM, 0);
        lv_obj_center(l);
    }

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// ── Screen 4: Astronomy ───────────────────────────────────────────────────────
static void build_astronomy_screen() {
    lv_obj_t *scr = screens[SCREEN_ASTRONOMY];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Astronomy - Sydney");

    // ── Layout ────────────────────────────────────────────────────────────────
    const int GAP    = 12;
    const int CARD_W = (SCREEN_W - NAV_BTN_W*2 - PAD*2 - GAP) / 2;
    const int CARD_H = CONTENT_H - PAD*2;
    const int TOP_Y  = CONTENT_Y + PAD;
    const int LEFT_X = NAV_BTN_W + PAD;
    const int RGHT_X = LEFT_X + CARD_W + GAP;
    const int IW     = CARD_W - PAD*2;   // inner width
    const int IH     = CARD_H - PAD*2;   // inner height

    // Fixed top section height — same on both cards for alignment
    const int TOP_H  = 200;   // header(20) + canvas(178) + small gap(2)
    const int HDR_H  = 20;
    const int ARC_H  = TOP_H - HDR_H - 2;

    // Row layout below divider — 6 rows each side
    const int ROWS   = 6;
    const int DIV_Y  = TOP_H + 8;        // divider y inside card interior
    const int ROW_H  = (IH - DIV_Y - 8) / ROWS;

    // Helper: one stat row — label left, value+sub right
    auto make_row = [&](lv_obj_t *card, int row,
                        const char *lbl,
                        lv_obj_t **val_out, lv_color_t val_col,
                        lv_obj_t **sub_out) {
        int ry = DIV_Y + 8 + row * ROW_H;
        lv_obj_t *l = lv_label_create(card);
        lv_label_set_text(l, lbl);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, C_DIM, 0);
        lv_obj_set_pos(l, 0, ry + (ROW_H - 16) / 2);

        lv_obj_t *v = lv_label_create(card);
        lv_label_set_text(v, "--");
        lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(v, val_col, 0);
        lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, ry);
        if (val_out) *val_out = v;

        if (sub_out) {
            lv_obj_t *s = lv_label_create(card);
            lv_label_set_text(s, "");
            lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(s, C_DIM, 0);
            lv_obj_align(s, LV_ALIGN_TOP_RIGHT, 0, ry + 22);
            *sub_out = s;
        }

        // Row divider line
        if (row < ROWS - 1) {
            lv_obj_t *rd = lv_obj_create(card);
            lv_obj_set_size(rd, IW, 1);
            lv_obj_set_pos(rd, 0, ry + ROW_H - 1);
            lv_obj_set_style_bg_color(rd, lv_color_hex(0x21262D), 0);
            lv_obj_set_style_bg_opa(rd, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(rd, 0, 0);
            lv_obj_set_style_pad_all(rd, 0, 0);
            lv_obj_clear_flag(rd, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
    };

    // ── SUN CARD (left) ───────────────────────────────────────────────────────
    lv_obj_t *sun_card = make_card(scr, LEFT_X, TOP_Y, CARD_W, CARD_H);

    make_label(sun_card, "Sun", &lv_font_montserrat_20,
               lv_color_hex(0xFFD033), LV_ALIGN_TOP_LEFT, 0, 0);

    // Arc canvas
    as_sun_buf = (lv_color_t *)heap_caps_malloc(
        AS_ARC_W * AS_ARC_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!as_sun_buf) as_sun_buf = (lv_color_t *)malloc(
        AS_ARC_W * AS_ARC_H * sizeof(lv_color_t));
    as_sun_canvas = lv_canvas_create(sun_card);
    lv_canvas_set_buffer(as_sun_canvas, as_sun_buf,
                         AS_ARC_W, AS_ARC_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(as_sun_canvas, 0, HDR_H + 2);
    lv_canvas_fill_bg(as_sun_canvas, C_CARD, LV_OPA_COVER);

    // Card divider
    lv_obj_t *sd = lv_obj_create(sun_card);
    lv_obj_set_size(sd, IW, 1);
    lv_obj_set_pos(sd, 0, DIV_Y);
    lv_obj_set_style_bg_color(sd, C_BORDER, 0);
    lv_obj_set_style_bg_opa(sd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sd, 0, 0);
    lv_obj_set_style_pad_all(sd, 0, 0);
    lv_obj_clear_flag(sd, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    make_row(sun_card, 0, "Sunrise",     &as_sunrise,    lv_color_hex(0xD29922), &as_sunrise_sub);
    make_row(sun_card, 1, "Solar noon",  &as_solarnoon,  lv_color_hex(0xFFD033), &as_solarnoon_sub);
    make_row(sun_card, 2, "Sunset",      &as_sunset,     C_WARM,                 &as_sunset_sub);
    make_row(sun_card, 3, "Day length",  &as_daylen,     C_TEXT,                 nullptr);
    make_row(sun_card, 4, "Next event",  &as_next_event, C_GREEN,                &as_next_event_sub);
    make_row(sun_card, 5, "Season",      &as_season,     C_WARM,                 nullptr);

    // ── MOON CARD (right) ─────────────────────────────────────────────────────
    lv_obj_t *moon_card = make_card(scr, RGHT_X, TOP_Y, CARD_W, CARD_H);

    make_label(moon_card, "Moon", &lv_font_montserrat_20,
               lv_color_hex(0xCDD9E5), LV_ALIGN_TOP_LEFT, 0, 0);

    // Moon disc canvas — same top section height as arc
    as_moon_buf = (lv_color_t *)heap_caps_malloc(
        AS_MOON_D * AS_MOON_D * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!as_moon_buf) as_moon_buf = (lv_color_t *)malloc(
        AS_MOON_D * AS_MOON_D * sizeof(lv_color_t));
    as_moon_canvas = lv_canvas_create(moon_card);
    lv_canvas_set_buffer(as_moon_canvas, as_moon_buf,
                         AS_MOON_D, AS_MOON_D, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(as_moon_canvas, 0, HDR_H + 2);
    lv_canvas_fill_bg(as_moon_canvas, C_CARD, LV_OPA_COVER);

    // Phase info alongside disc — vertically centred in top section
    const int DISC_TEXT_X = AS_MOON_D + 16;
    as_moon_phase = make_label(moon_card, "--",
                               &lv_font_montserrat_20, C_ACCENT,
                               LV_ALIGN_TOP_LEFT, DISC_TEXT_X, HDR_H + 10);
    as_moon_illum = make_label(moon_card, "--% illuminated",
                               &lv_font_montserrat_16, C_DIM,
                               LV_ALIGN_TOP_LEFT, DISC_TEXT_X, HDR_H + 38);
    as_moon_age   = make_label(moon_card, "Age: -- days",
                               &lv_font_montserrat_16, C_DIM,
                               LV_ALIGN_TOP_LEFT, DISC_TEXT_X, HDR_H + 62);

    // Card divider
    lv_obj_t *md = lv_obj_create(moon_card);
    lv_obj_set_size(md, IW, 1);
    lv_obj_set_pos(md, 0, DIV_Y);
    lv_obj_set_style_bg_color(md, C_BORDER, 0);
    lv_obj_set_style_bg_opa(md, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(md, 0, 0);
    lv_obj_set_style_pad_all(md, 0, 0);
    lv_obj_clear_flag(md, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    make_row(moon_card, 0, "Moonrise",       &as_moonrise,     lv_color_hex(0xCDD9E5), &as_moonrise_sub);
    make_row(moon_card, 1, "Moonset",        &as_moonset,      C_DIM,                  &as_moonset_sub);
    make_row(moon_card, 2, "Illumination",   &as_moon_illum_pct, C_COOL,               nullptr);
    make_row(moon_card, 3, "Moon age",       &as_moon_age_val, C_TEXT,                 nullptr);
    make_row(moon_card, 4, "Next full moon", &as_next_full,    lv_color_hex(0xCDD9E5), nullptr);
    make_row(moon_card, 5, "Next new moon",  &as_next_new,     C_DIM,                  nullptr);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  CELESTIAL MATH HELPERS  (ported from original project, Southern Hemisphere)
// =============================================================================

static int day_of_year(int day, int month, int year) {
    int dpm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = (year%4==0 && year%100!=0) || (year%400==0);
    if (leap) dpm[1] = 29;
    int doy = 0;
    for (int m = 0; m < month-1; m++) doy += dpm[m];
    return doy + day;
}

struct SeasonEvts { int mar, jun, sep, dec; };
static SeasonEvts calc_season_events(int year) {
    float y = year + 0.5f;
    SeasonEvts e;
    e.mar = (int)(79.3125f  + 0.2422f*(y-2000) - (int)((y-2000)/4.0f));
    e.jun = (int)(171.3125f + 0.2422f*(y-2000) - (int)((y-2000)/4.0f));
    e.sep = (int)(264.3125f + 0.2422f*(y-2000) - (int)((y-2000)/4.0f));
    e.dec = (int)(354.3125f + 0.2422f*(y-2000) - (int)((y-2000)/4.0f));
    return e;
}

static float earth_orbit_angle(int doy, const SeasonEvts &e) {
    // Orbit runs clockwise on screen: Dec Sol (right,0deg) -> Mar Eq (top,270deg)
    //   -> Jun Sol (left,180deg) -> Sep Eq (bottom,90deg)
    // Raw calculation was 90deg too high in every segment; subtract 90 and normalise.
    float raw;
    if      (doy >= e.dec)                   raw = 90.0f  - (float)(doy-e.dec)         / (float)(e.mar+365-e.dec) * 90.0f;
    else if (doy <  e.mar)                   raw = 90.0f  - (float)(doy+365-e.dec)     / (float)(e.mar+365-e.dec) * 90.0f;
    else if (doy >= e.mar && doy < e.jun)    raw = 360.0f - (float)(doy-e.mar)         / (float)(e.jun-e.mar)     * 90.0f;
    else if (doy >= e.jun && doy < e.sep)    raw = 270.0f - (float)(doy-e.jun)         / (float)(e.sep-e.jun)     * 90.0f;
    else                                     raw = 180.0f - (float)(doy-e.sep)         / (float)(e.dec-e.sep)     * 90.0f;
    float angle = raw - 90.0f;
    if (angle < 0.0f) angle += 360.0f;
    return angle;
}

static const char* doy_to_date_str(int doy, int year) {
    // Two alternating buffers so two calls in the same snprintf() don't alias
    static char bufs[2][12];
    static int  idx = 0;
    char *buf = bufs[idx & 1];
    idx++;
    int dpm[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    bool leap = (year%4==0 && year%100!=0) || (year%400==0);
    if (leap) dpm[1] = 29;
    int m = 0, d = doy;
    while (m < 12 && d > dpm[m]) { d -= dpm[m]; m++; }
    snprintf(buf, 12, "%d %s", d, months[m]);
    return buf;
}

// Parse "06:42 AM" → minutes since midnight
static int parse_time_to_mins(const char *s) {
    int h = 0, m = 0;
    char ampm[4] = {};
    sscanf(s, "%d:%d %3s", &h, &m, ampm);
    if ((ampm[0]=='P'||ampm[0]=='p') && h != 12) h += 12;
    if ((ampm[0]=='A'||ampm[0]=='a') && h == 12) h = 0;
    return h*60 + m;
}

// Minutes → "HH:MM" string
static const char* mins_to_time_str(int mins) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", (mins/60)%24, mins%60);
    return buf;
}

// Lunar age (days since last new moon) from a known epoch
static float lunar_age_now() {
    // Known new moon epoch: 6 Jan 2000 18:14 UTC  (Unix = 947182440)
    const time_t LUNA_EPOCH = 947182440L;
    const float  CYCLE_SECS = 29.53f * 86400.0f;
    time_t now = time(nullptr);
    float elapsed = fmodf((float)(now - LUNA_EPOCH), CYCLE_SECS);
    if (elapsed < 0) elapsed += CYCLE_SECS;
    return elapsed / 86400.0f;
}

// ── LVGL canvas helper — draw a filled circle ────────────────────────────────
static void canvas_fill_circle(lv_obj_t *cv, int32_t cx, int32_t cy,
                                int32_t r, lv_color_t col) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color  = col;
    dsc.bg_opa    = LV_OPA_COVER;
    dsc.radius    = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;
    lv_area_t a = { cx-r, cy-r, cx+r, cy+r };
    lv_canvas_draw_rect(cv, a.x1, a.y1, r*2, r*2, &dsc);
}

static void canvas_stroke_circle(lv_obj_t *cv, int32_t cx, int32_t cy,
                                  int32_t r, lv_color_t col, uint16_t w=2) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa      = LV_OPA_TRANSP;
    dsc.border_color = col;
    dsc.border_opa   = LV_OPA_COVER;
    dsc.border_width = w;
    dsc.radius       = LV_RADIUS_CIRCLE;
    lv_canvas_draw_rect(cv, cx-r, cy-r, r*2, r*2, &dsc);
}

static void canvas_line(lv_obj_t *cv, int32_t x1, int32_t y1,
                         int32_t x2, int32_t y2, lv_color_t col, uint16_t w=2) {
    lv_point_t pts[2] = {{(lv_coord_t)x1,(lv_coord_t)y1},{(lv_coord_t)x2,(lv_coord_t)y2}};
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = col;
    dsc.width = w;
    lv_canvas_draw_line(cv, pts, 2, &dsc);
}

// Draw sun icon (filled circle with spokes)
static void draw_sun_icon(lv_obj_t *cv, int cx, int cy, int r,
                           lv_color_t sunCol, lv_color_t bgCol) {
    canvas_fill_circle(cv, cx, cy, r, sunCol);
    canvas_fill_circle(cv, cx, cy, r/2, bgCol);
    for (int i = 0; i < 8; i++) {
        float a = i * M_PI / 4.0f;
        int x1 = cx + (int)((r+3)*cosf(a)), y1 = cy + (int)((r+3)*sinf(a));
        int x2 = cx + (int)((r+8)*cosf(a)), y2 = cy + (int)((r+8)*sinf(a));
        canvas_line(cv, x1, y1, x2, y2, sunCol, 2);
    }
}

// Draw moon disc for given lunar age (0-29.5 days)
static void draw_moon_disc(lv_obj_t *cv, int cx, int cy, int r, float age,
                            lv_color_t moonCol, lv_color_t bgCol, lv_color_t darkCol) {
    // Draw a simplified but correct phase shape using arc segments on the canvas
    // For LVGL simplicity: use pixel-by-pixel approach via lv_canvas_set_px
    bool waxing = age < 14.765f;
    float norm  = age / 29.53f;
    // Terminator x-offset: cos(phase*π) gives shadow extent
    float phase = waxing ? norm * 2.0f : (norm - 0.5f) * 2.0f;
    float termOff = cosf(phase * M_PI) * r;  // positive = illuminated on right

    for (int dy = -r; dy <= r; dy++) {
        int hw = (int)sqrtf((float)(r*r - dy*dy));
        for (int dx = -hw; dx <= hw; dx++) {
            bool lit;
            if (waxing) {
                // Right half lit; terminator offsets inward from left
                lit = (dx >= (int)(-termOff));
            } else {
                // Left half lit; terminator offsets inward from right
                lit = (dx <= (int)(termOff));
            }
            lv_color_t px = lit ? moonCol : darkCol;
            lv_canvas_set_px_color(cv, cx+dx, cy+dy, px);
        }
    }
    canvas_stroke_circle(cv, cx, cy, r, C_DIM, 1);
}
static int wx_icon_category(int code, bool isDay = true) {
    // 1000=sunny/clear, 1003=partly cloudy, 1006/1009=cloudy/overcast
    // 1030/1135/1147=fog/mist, 1063-1201=rain family,
    // 1204-1237=sleet/snow, 1273-1282=thunder
    if (code == 1000)                          return isDay ? 0 : 7; // sunny / clear night
    if (code == 1003)                          return 1;  // partly cloudy
    if (code == 1006 || code == 1009)          return 2;  // cloudy
    if (code == 1030 || code == 1135 || code == 1147) return 5; // fog
    if (code >= 1273)                          return 4;  // thunder
    if (code >= 1204 && code <= 1237)          return 6;  // snow
    if (code >= 1063)                          return 3;  // rain
    return 2;  // default cloudy
}

// Draw a weather icon onto a canvas (OUTLOOK_ICON_W × OUTLOOK_ICON_H)
// category: 0=sunny 1=partcloud 2=cloudy 3=rain 4=thunder 5=fog 6=snow
static void draw_wx_icon(lv_obj_t *cv, int cat) {
    lv_canvas_fill_bg(cv, C_BG, LV_OPA_COVER);

    const int W  = OUTLOOK_ICON_W;
    const int H  = OUTLOOK_ICON_H;
    const int cx = W/2;
    const int cy = H/2 - 4;

    lv_color_t sunYellow  = lv_color_hex(0xFFD033);
    lv_color_t cloudGrey  = lv_color_hex(0x8B949E);
    lv_color_t rainBlue   = lv_color_hex(0x79C0FF);
    lv_color_t snowWhite  = lv_color_hex(0xCDD9E5);
    lv_color_t boltYellow = lv_color_hex(0xD29922);
    lv_color_t fogGrey    = lv_color_hex(0x6E7681);

    // ── Helper: draw a cloud shape centred at (ccx, ccy), half-width cw ────────
    auto draw_cloud = [&](int ccx, int ccy, int cw, lv_color_t col) {
        int ch = cw * 5 / 8;
        // Three overlapping circles forming a cloud
        canvas_fill_circle(cv, ccx,          ccy,      ch/2,   col);
        canvas_fill_circle(cv, ccx - cw/3,   ccy+ch/5, ch*2/5, col);
        canvas_fill_circle(cv, ccx + cw/3,   ccy+ch/5, ch*2/5, col);
        // Fill the bottom rectangle to join them
        for (int dy = 0; dy <= ch/2; dy++) {
            int hw = cw/2;
            canvas_line(cv, ccx-hw, ccy+dy, ccx+hw, ccy+dy, col, 1);
        }
    };

    // ── Helper: draw sun (circle + 8 spokes) ────────────────────────────────
    auto draw_sun = [&](int sx, int sy, int r, lv_color_t col) {
        canvas_fill_circle(cv, sx, sy, r, col);
        for (int i = 0; i < 8; i++) {
            float a = i * M_PI / 4.0f;
            int x1 = sx + (int)((r+3)*cosf(a)), y1 = sy + (int)((r+3)*sinf(a));
            int x2 = sx + (int)((r+8)*cosf(a)), y2 = sy + (int)((r+8)*sinf(a));
            canvas_line(cv, x1, y1, x2, y2, col, 2);
        }
    };

    switch (cat) {
    case 0:  // ── SUNNY ─────────────────────────────────────────────────────
        draw_sun(cx, cy, 20, sunYellow);
        break;

    case 1:  // ── PARTLY CLOUDY ──────────────────────────────────────────────
        draw_sun(cx-8, cy-8, 14, sunYellow);
        draw_cloud(cx+6, cy+8, 30, cloudGrey);
        break;

    case 2:  // ── CLOUDY ────────────────────────────────────────────────────
        draw_cloud(cx, cy, 36, cloudGrey);
        break;

    case 3:  // ── RAIN ──────────────────────────────────────────────────────
        draw_cloud(cx, cy-6, 32, cloudGrey);
        // Rain drops
        for (int i = 0; i < 4; i++) {
            int rx = cx - 18 + i*12;
            canvas_line(cv, rx, cy+18, rx-3, cy+28, rainBlue, 2);
        }
        break;

    case 4:  // ── THUNDER ───────────────────────────────────────────────────
        draw_cloud(cx, cy-6, 32, cloudGrey);
        // Zigzag bolt
        canvas_line(cv, cx+4,  cy+16, cx-2,  cy+24, boltYellow, 3);
        canvas_line(cv, cx-2,  cy+24, cx+6,  cy+24, boltYellow, 3);
        canvas_line(cv, cx+6,  cy+24, cx-4,  cy+36, boltYellow, 3);
        break;

    case 5:  // ── FOG ───────────────────────────────────────────────────────
        for (int i = 0; i < 4; i++) {
            int fy = cy - 12 + i*10;
            canvas_line(cv, cx-28, fy, cx+28, fy, fogGrey, 3);
        }
        break;

    case 6:  // ── SNOW ──────────────────────────────────────────────────────
        draw_cloud(cx, cy-6, 32, cloudGrey);
        // Snow dots
        for (int i = 0; i < 4; i++) {
            int sx2 = cx - 18 + i*12;
            canvas_fill_circle(cv, sx2, cy+24, 2, snowWhite);
            canvas_fill_circle(cv, sx2-3, cy+32, 2, snowWhite);
        }
        break;

    case 7:  // ── CLEAR NIGHT (crescent moon) ───────────────────────────────
    {
        // Crescent: large bright disc with a smaller offset disc cut out
        lv_color_t moonCol = lv_color_hex(0xCDD9E5);
        canvas_fill_circle(cv, cx, cy, 18, moonCol);
        // "Cut" the crescent by overdrawing with bg colour, offset left+up
        canvas_fill_circle(cv, cx - 8, cy - 6, 16, C_BG);
        // A couple of small star dots for context
        canvas_fill_circle(cv, cx + 20, cy - 14, 2, lv_color_hex(0x8B949E));
        canvas_fill_circle(cv, cx + 26, cy + 4,  1, lv_color_hex(0x8B949E));
        canvas_fill_circle(cv, cx + 14, cy + 20, 1, lv_color_hex(0x8B949E));
    }
    break;

    default:
        draw_cloud(cx, cy, 32, cloudGrey);
        break;
    }
}

// Canvas dimensions for celestial screens — use the full content area minus nav
#define CEL_W   (SCREEN_W - NAV_BTN_W * 2)
#define CEL_H   (SCREEN_H - STATUSBAR_H - 80)   // kept for lunar screen

static lv_color_t *sea_canvas_buf = nullptr;
static lv_color_t *lun_canvas_buf = nullptr;

// =============================================================================
//  SCREEN 5: SOLAR ELEVATION CHART
//  Full-day elevation curve with twilight zones, current sun position,
//  and a stat row showing key times. Updated every minute via render_solar().
//  Maths: Spencer solar declination + equation of time, no external API.
// =============================================================================

// ── Solar maths helpers ───────────────────────────────────────────────────────
// Solar declination (degrees) from day-of-year — Spencer (1971)
static float solar_decl_deg(int doy) {
    float B = 2.0f * (float)M_PI * (doy - 1) / 365.0f;
    return (180.0f / (float)M_PI) * (
        0.006918f
      - 0.399912f * cosf(B) + 0.070257f * sinf(B)
      - 0.006758f * cosf(2*B) + 0.000907f * sinf(2*B)
      - 0.002697f * cosf(3*B) + 0.001480f * sinf(3*B));
}

// Equation of time (minutes) — corrects for Earth's elliptical orbit
static float solar_eqtime_min(int doy) {
    float B = 2.0f * (float)M_PI * (doy - 1) / 365.0f;
    return 229.18f * (
        0.000075f
      + 0.001868f * cosf(B)   - 0.032077f * sinf(B)
      - 0.014615f * cosf(2*B) - 0.040890f * sinf(2*B));
}

// Solar elevation (degrees) for a given local hour (0-24), lat/lon (degrees), doy
// lon_offset_hrs = hours ahead of UTC (AEST=10, AEDT=11)
static float solar_elevation(float local_hr, float lat_deg, float lon_deg,
                               float decl_deg, float eqtime_min, int utc_offset_hrs) {
    // Longitude correction relative to timezone standard meridian (150° for AEST)
    float std_meridian = (float)utc_offset_hrs * 15.0f;
    float lon_corr_min = (lon_deg - std_meridian) * 4.0f;
    float solar_hr     = local_hr + (eqtime_min + lon_corr_min) / 60.0f;
    float ha_deg       = (solar_hr - 12.0f) * 15.0f;     // hour angle
    float ha_r  = ha_deg  * (float)M_PI / 180.0f;
    float lat_r = lat_deg * (float)M_PI / 180.0f;
    float dec_r = decl_deg * (float)M_PI / 180.0f;
    return (180.0f / (float)M_PI) * asinf(
        sinf(lat_r) * sinf(dec_r) +
        cosf(lat_r) * cosf(dec_r) * cosf(ha_r));
}

// Solar azimuth (degrees, 0=N clockwise) for same parameters
static float solar_azimuth(float local_hr, float lat_deg, float lon_deg,
                             float decl_deg, float eqtime_min, int utc_offset_hrs) {
    float std_meridian = (float)utc_offset_hrs * 15.0f;
    float lon_corr_min = (lon_deg - std_meridian) * 4.0f;
    float solar_hr     = local_hr + (eqtime_min + lon_corr_min) / 60.0f;
    float ha_deg       = (solar_hr - 12.0f) * 15.0f;
    float ha_r  = ha_deg  * (float)M_PI / 180.0f;
    float lat_r = lat_deg * (float)M_PI / 180.0f;
    float dec_r = decl_deg * (float)M_PI / 180.0f;
    float elev_r = asinf(sinf(lat_r)*sinf(dec_r) + cosf(lat_r)*cosf(dec_r)*cosf(ha_r));
    float cos_az = (sinf(dec_r) - sinf(elev_r)*sinf(lat_r)) / (cosf(elev_r)*cosf(lat_r));
    if (cos_az >  1.0f) cos_az =  1.0f;
    if (cos_az < -1.0f) cos_az = -1.0f;
    float az = (180.0f / (float)M_PI) * acosf(cos_az);
    return (ha_r > 0) ? (360.0f - az) : az;
}

// ── Build ────────────────────────────────────────────────────────────────────
static void build_solar_screen() {
    lv_obj_t *scr = screens[SCREEN_SOLAR];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Solar Elevation - Sydney");

    const int CY     = STATUSBAR_H;
    const int CH     = SCREEN_H - STATUSBAR_H - 26;   // 650
    const int CARD_X = NAV_BTN_W;                      // 70
    const int CARD_W = SCREEN_W - NAV_BTN_W * 2;       // 1140
    const int GAP    = 10;

    // ── Chart card ────────────────────────────────────────────────────────────
    const int STATS_CARD_H  = 110;                                    // tight fit
    const int CHART_CARD_H  = CH - PAD * 2 - STATS_CARD_H - GAP;     // fills remaining space
    lv_obj_t *chart_card = make_card_nopad(scr, CARD_X, CY + PAD,
                                            CARD_W, CHART_CARD_H);

    sol_canvas_buf = (lv_color_t *)heap_caps_malloc(
        SOL_CHART_W * SOL_CHART_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sol_canvas_buf) sol_canvas_buf = (lv_color_t *)malloc(
        SOL_CHART_W * SOL_CHART_H * sizeof(lv_color_t));

    sol_canvas = lv_canvas_create(chart_card);
    lv_canvas_set_buffer(sol_canvas, sol_canvas_buf,
                         SOL_CHART_W, SOL_CHART_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(sol_canvas, 0, PAD / 2);
    lv_canvas_fill_bg(sol_canvas, C_BG, LV_OPA_COVER);

    // ── Stats card (below chart) ──────────────────────────────────────────────
    const int STATS_Y    = CY + PAD + CHART_CARD_H + GAP;
    lv_obj_t *stats_card = make_card_nopad(scr, CARD_X, STATS_Y,
                                            CARD_W, STATS_CARD_H);
    lv_obj_set_style_pad_all(stats_card, PAD, 0);

    // 6 stat columns: Sunrise | Solar Noon | Sunset | Day Length | Peak Elev | Current
    const int NCOLS  = 6;
    const int INNER_W = CARD_W - PAD * 2;
    const int COL_W   = (INNER_W - 10 * (NCOLS - 1)) / NCOLS;   // ~165px

    auto make_stat = [&](int col, const char *lbl, lv_color_t vcol,
                          lv_obj_t **val_out, lv_obj_t **sub_out) {
        int sx = col * (COL_W + 10);
        make_label(stats_card, lbl, &lv_font_montserrat_14, C_DIM,
                   LV_ALIGN_TOP_LEFT, sx, 0);
        *val_out = lv_label_create(stats_card);
        lv_label_set_text(*val_out, "--");
        lv_obj_set_style_text_font(*val_out, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(*val_out, vcol, 0);
        lv_obj_set_pos(*val_out, sx, 18);
        lv_obj_clear_flag(*val_out, LV_OBJ_FLAG_CLICKABLE);
        if (sub_out) {
            *sub_out = lv_label_create(stats_card);
            lv_label_set_text(*sub_out, "--");
            lv_obj_set_style_text_font(*sub_out, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(*sub_out, C_DIM, 0);
            lv_obj_set_pos(*sub_out, sx, 56);
            lv_obj_clear_flag(*sub_out, LV_OBJ_FLAG_CLICKABLE);
        }
    };

    make_stat(0, "Sunrise",      lv_color_hex(0xD29922), &sol_sunrise,  &sol_sunrise_cd);
    make_stat(1, "Solar noon",   lv_color_hex(0xFFD033), &sol_noon,     &sol_noon_cd);
    make_stat(2, "Sunset",       C_WARM,                 &sol_sunset,   &sol_sunset_cd);
    make_stat(3, "Day length",   C_TEXT,                 &sol_daylen,   nullptr);
    make_stat(4, "Peak elev.",   lv_color_hex(0xFFB700), &sol_peak,     nullptr);
    make_stat(5, "Now",          C_ACCENT,               &sol_cur_elev, &sol_cur_az);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// ── Render ────────────────────────────────────────────────────────────────────
// Called every minute from ui_update_celestial().
// Uses RTC date for declination, WeatherAPI sunrise/sunset for accuracy.
static void render_solar(const RtcDateTime &dt, const WeatherResult &wr) {
    if (!sol_canvas || !sol_canvas_buf) return;

    // ── Time / date context ───────────────────────────────────────────────────
    time_t now_t = time(nullptr);
    struct tm *lt = localtime(&now_t);
    int curMin    = lt->tm_hour * 60 + lt->tm_min;
    float curHr   = lt->tm_hour + lt->tm_min / 60.0f;

    int doy = day_of_year(dt.day, dt.month, dt.year);

    // Determine UTC offset from POSIX TZ (AEDT=11 in summer, AEST=10 in winter)
    // Use localtime offset from UTC
    // UTC offset in hours — compute by comparing UTC epoch with local broken-down time.
    // mktime() treats its input as LOCAL time and returns UTC seconds.
    // So mktime(localtime(t)) == t always. Instead we want the inverse:
    // treat the local broken-down time AS IF it were UTC, then diff against real UTC.
    // offset = (local_hour*3600 + local_min*60 + local_sec) - (utc_hour*3600 + ...)
    // Simplest reliable method on ESP32: use the POSIX timezone offset directly.
    // setenv("TZ",...) was called in setup so timezone global is populated.
    // _timezone (seconds west of UTC, POSIX) gives us what we need:
    //   AEST = -36000, AEDT = -39600  (negative = east of UTC)
    // utc_offset_hrs = -_timezone / 3600  (convert west→east, seconds→hours)
    // _timezone may not account for DST; add lt->tm_isdst hours if DST active.
    int utc_offset_hrs = (int)(-_timezone / 3600);
    if (lt->tm_isdst > 0) utc_offset_hrs += 1;

    const float LAT = (float)BOM_TIDE_LAT;   // -33.8568 (Fort Denison, Sydney)
    const float LON = (float)BOM_TIDE_LON;   // 151.2253

    float decl  = solar_decl_deg(doy);
    float eqt   = solar_eqtime_min(doy);

    // Solar noon in local time
    float std_meridian = (float)utc_offset_hrs * 15.0f;
    float lon_corr_min = (LON - std_meridian) * 4.0f;
    float solar_noon_hr = 12.0f - eqt / 60.0f - lon_corr_min / 60.0f;

    // Peak elevation (at solar noon)
    float peak_elev = solar_elevation(solar_noon_hr, LAT, LON, decl, eqt, utc_offset_hrs);

    // Use WeatherAPI sunrise/sunset if available, else compute from elevation zero-crossing
    int srMins = wr.valid ? parse_time_to_mins(wr.sunrise) : -1;
    int ssMins = wr.valid ? parse_time_to_mins(wr.sunset)  : -1;

    // ── Draw chart canvas ─────────────────────────────────────────────────────
    lv_obj_clean(sol_canvas);
    lv_canvas_set_buffer(sol_canvas, sol_canvas_buf,
                         SOL_CHART_W, SOL_CHART_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(sol_canvas, C_BG, LV_OPA_COVER);

    const int W = SOL_CHART_W, H = SOL_CHART_H;
    const int PAD_L = 48, PAD_R = 16, PAD_T = 16, PAD_B = 30;
    const int CW = W - PAD_L - PAD_R;
    const int CH = H - PAD_T - PAD_B;

    const float ELEV_MAX =  80.0f;
    const float ELEV_MIN = -22.0f;
    const float ELEV_RNG =  ELEV_MAX - ELEV_MIN;

    // Map elevation and hour to canvas coordinates
    auto toY = [&](float elev) -> int {
        return PAD_T + (int)((ELEV_MAX - elev) / ELEV_RNG * CH);
    };
    auto toX = [&](float hr) -> int {
        return PAD_L + (int)(hr / 24.0f * CW);
    };

    // ── Horizon fill (below=near black, above=deep sky blue bands) ────────────
    // Fill entire chart area first
    for (int y = PAD_T; y < H - PAD_B; y++)
        canvas_line(sol_canvas, PAD_L, y, W - PAD_R, y, C_BG, 1);

    // Twilight / sky zone bands (drawn bottom to top)
    struct { float lo, hi; uint32_t col; } zones[] = {
        { ELEV_MIN, -18.0f, 0x050508 },   // night
        {   -18.0f, -12.0f, 0x080C18 },   // astronomical twilight
        {   -12.0f,  -6.0f, 0x0D1428 },   // nautical twilight
        {    -6.0f,   0.0f, 0x162040 },   // civil twilight
        {     0.0f,  20.0f, 0x0A1E30 },   // low sun
        {    20.0f,  50.0f, 0x091828 },   // mid sun
        {    50.0f, ELEV_MAX, 0x071020 }, // high sun
    };
    for (auto &z : zones) {
        int y1 = toY(z.hi);
        int y2 = toY(z.lo);
        if (y1 < PAD_T)    y1 = PAD_T;
        if (y2 > H-PAD_B)  y2 = H - PAD_B;
        for (int y = y1; y < y2; y++)
            canvas_line(sol_canvas, PAD_L, y, W - PAD_R, y,
                        lv_color_hex(z.col), 1);
    }

    // ── Grid lines & Y-axis labels ────────────────────────────────────────────
    // Horizon (0°) — prominent
    canvas_line(sol_canvas, PAD_L, toY(0), W - PAD_R, toY(0), C_BORDER, 2);

    // Elevation grid at -18, -12, -6, 0, 20, 40, 60, 80
    float gridElev[] = { -18, -12, -6, 20, 40, 60 };
    lv_color_t gridCol[] = {
        lv_color_hex(0x1C2440), lv_color_hex(0x1C2840), lv_color_hex(0x1C3050),
        lv_color_hex(0x182030), lv_color_hex(0x141A28), lv_color_hex(0x101420)
    };
    const char *gridLbl[] = { "-18", "-12", "-6", "20", "40", "60" };
    for (int gi = 0; gi < 6; gi++) {
        int gy = toY(gridElev[gi]);
        canvas_line(sol_canvas, PAD_L, gy, W - PAD_R, gy, gridCol[gi], 1);
        canvas_line(sol_canvas, PAD_L - 4, gy, PAD_L, gy, C_DIM, 1);
        // Y label
        lv_obj_t *yl = lv_label_create(sol_canvas);
        lv_label_set_text(yl, gridLbl[gi]);
        lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(yl, C_DIM, 0);
        lv_obj_set_style_bg_opa(yl, LV_OPA_TRANSP, 0);
        lv_obj_set_size(yl, PAD_L - 4, 16);
        lv_obj_set_style_text_align(yl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(yl, 0, gy - 8);
    }
    // 0° label
    {
        lv_obj_t *yl = lv_label_create(sol_canvas);
        lv_label_set_text(yl, "0");
        lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(yl, C_DIM, 0);
        lv_obj_set_style_bg_opa(yl, LV_OPA_TRANSP, 0);
        lv_obj_set_size(yl, PAD_L - 4, 16);
        lv_obj_set_style_text_align(yl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(yl, 0, toY(0) - 8);
    }

    // Twilight zone labels — centred vertically within each zone band
    // Place label midway between the zone's two elevation boundaries
    struct { float lo, hi; const char *lbl; } twilbls[] = {
        { -18.0f, -12.0f, "Astronomical twilight" },
        { -12.0f,  -6.0f, "Nautical twilight" },
        {  -6.0f,   0.0f, "Civil twilight" },
    };
    for (auto &tw : twilbls) {
        float mid = (tw.lo + tw.hi) / 2.0f;
        int ty = toY(mid) - 8;   // centre of font_14 (~16px tall)
        lv_obj_t *tl = lv_label_create(sol_canvas);
        lv_label_set_text(tl, tw.lbl);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tl, lv_color_hex(0x3A5070), 0);
        lv_obj_set_style_bg_opa(tl, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(tl, PAD_L + 6, ty);
    }

    // ── X-axis hour labels & vertical grid ────────────────────────────────────
    int xhours[] = { 0, 3, 6, 9, 12, 15, 18, 21 };
    const char *xlbls[] = { "0h","3h","6h","9h","12h","15h","18h","21h" };
    for (int xi = 0; xi < 8; xi++) {
        int gx = toX((float)xhours[xi]);
        canvas_line(sol_canvas, gx, PAD_T, gx, H - PAD_B,
                    lv_color_hex(0x18202C), 1);
        lv_obj_t *xl = lv_label_create(sol_canvas);
        lv_label_set_text(xl, xlbls[xi]);
        lv_obj_set_style_text_font(xl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(xl, C_DIM, 0);
        lv_obj_set_style_bg_opa(xl, LV_OPA_TRANSP, 0);
        lv_obj_set_size(xl, 30, 16);
        lv_obj_set_style_text_align(xl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(xl, gx - 15, H - PAD_B + 6);
    }

    // Solar noon dashed vertical
    {
        int nx = toX(solar_noon_hr);
        for (int yy = PAD_T; yy < H - PAD_B; yy += 8)
            canvas_line(sol_canvas, nx, yy, nx, yy + 4,
                        lv_color_hex(0x2A2200), 1);
    }

    // ── Elevation curve ───────────────────────────────────────────────────────
    // Compute 241 sample points (every 6 minutes)
    const int NPTS = 241;
    int px_arr[NPTS], py_arr[NPTS];
    for (int i = 0; i < NPTS; i++) {
        float hr  = i * 24.0f / (NPTS - 1);
        float elev = solar_elevation(hr, LAT, LON, decl, eqt, utc_offset_hrs);
        px_arr[i] = toX(hr);
        py_arr[i] = toY(elev);
    }

    // Warm fill under curve (daytime only — above horizon)
    lv_color_t fill_col = lv_color_hex(0x1A1200);
    int hz_y = toY(0.0f);
    for (int xi = PAD_L; xi < W - PAD_R; xi++) {
        float frac = (float)(xi - PAD_L) / (float)CW;
        float hr   = frac * 24.0f;
        float elev = solar_elevation(hr, LAT, LON, decl, eqt, utc_offset_hrs);
        if (elev > 0.0f) {
            int ly = toY(elev);
            if (ly < hz_y)
                canvas_line(sol_canvas, xi, ly, xi, hz_y - 1, fill_col, 1);
        }
    }

    // Below-horizon curve — dim grey
    for (int i = 0; i < NPTS - 1; i++) {
        float hr = i * 24.0f / (NPTS - 1);
        float elev = solar_elevation(hr, LAT, LON, decl, eqt, utc_offset_hrs);
        if (elev < 0.0f) {
            canvas_line(sol_canvas, px_arr[i], py_arr[i],
                                    px_arr[i+1], py_arr[i+1],
                        lv_color_hex(0x303840), 1);
        }
    }

    // Above-horizon curve — gold, 2px thick
    for (int i = 0; i < NPTS - 1; i++) {
        float hr  = i * 24.0f / (NPTS - 1);
        float hr1 = (i+1) * 24.0f / (NPTS - 1);
        float e0  = solar_elevation(hr,  LAT, LON, decl, eqt, utc_offset_hrs);
        float e1  = solar_elevation(hr1, LAT, LON, decl, eqt, utc_offset_hrs);
        if (e0 >= 0.0f || e1 >= 0.0f) {
            lv_color_t lc = lv_color_hex(0xD29922);
            canvas_line(sol_canvas, px_arr[i],   py_arr[i],
                                    px_arr[i+1], py_arr[i+1], lc, 1);
            canvas_line(sol_canvas, px_arr[i],   py_arr[i]-1,
                                    px_arr[i+1], py_arr[i+1]-1, lc, 1);
        }
    }

    // ── Current time marker ───────────────────────────────────────────────────
    float curElev = solar_elevation(curHr, LAT, LON, decl, eqt, utc_offset_hrs);
    float curAz   = solar_azimuth(curHr, LAT, LON, decl, eqt, utc_offset_hrs);
    int   cx_now  = toX(curHr);
    // Clamp cy_now to canvas bounds so the marker is always visible
    int   cy_now  = toY(curElev);
    if (cy_now < PAD_T + 14)  cy_now = PAD_T + 14;
    if (cy_now > H - PAD_B - 6) cy_now = H - PAD_B - 6;

    // Vertical current-time line (blue dashed)
    for (int yy = PAD_T; yy < H - PAD_B; yy += 6)
        canvas_line(sol_canvas, cx_now, yy, cx_now, yy + 3,
                    C_ACCENT, 1);

    if (curElev >= -1.0f) {
        // Sun above or near horizon — draw sun icon
        draw_sun_icon(sol_canvas, cx_now, cy_now, 13,
                      lv_color_hex(0xFFB700), C_BG);
    } else {
        // Well below horizon — accent dot on curve
        canvas_fill_circle(sol_canvas, cx_now, cy_now, 5, C_ACCENT);
    }

    // Current elevation annotation
    {
        char annot[16];
        snprintf(annot, sizeof(annot), "%.0f%s", curElev,
                 curElev >= 0 ? "\xC2\xB0" : "\xC2\xB0");
        int ax = cx_now + 14;
        if (ax + 40 > W - PAD_R) ax = cx_now - 54;
        lv_obj_t *al = lv_label_create(sol_canvas);
        lv_label_set_text(al, annot);
        lv_obj_set_style_text_font(al, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(al, C_ACCENT, 0);
        lv_obj_set_style_bg_opa(al, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(al, ax, cy_now - 8);
    }

    lv_obj_invalidate(sol_canvas);

    // ── Update stat labels ────────────────────────────────────────────────────
    char buf[32];
    auto fmt_cd = [](int diff, char *out, size_t n) {
        int ad = diff < 0 ? -diff : diff;
        const char *dir = diff < 0 ? "ago" : "away";
        if (ad < 60) snprintf(out, n, "%dm %s", ad, dir);
        else         snprintf(out, n, "%dh %dm %s", ad/60, ad%60, dir);
    };

    if (wr.valid) {
        lv_label_set_text(sol_sunrise, wr.sunrise);
        fmt_cd(srMins - curMin, buf, sizeof(buf));
        lv_label_set_text(sol_sunrise_cd, buf);

        lv_label_set_text(sol_sunset, wr.sunset);
        fmt_cd(ssMins - curMin, buf, sizeof(buf));
        lv_label_set_text(sol_sunset_cd, buf);

        int dayLen = ssMins - srMins;
        snprintf(buf, sizeof(buf), "%dh %dm", dayLen/60, dayLen%60);
        lv_label_set_text(sol_daylen, buf);
    }

    // Solar noon
    lv_label_set_text(sol_noon, mins_to_time_str((int)(solar_noon_hr * 60)));
    int noonMin = (int)(solar_noon_hr * 60);
    fmt_cd(noonMin - curMin, buf, sizeof(buf));
    lv_label_set_text(sol_noon_cd, buf);

    // Peak elevation
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", peak_elev);
    lv_label_set_text(sol_peak, buf);

    // Current elevation + azimuth
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", curElev);
    lv_label_set_text(sol_cur_elev, buf);
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0 az", curAz);
    lv_label_set_text(sol_cur_az, buf);
}

// =============================================================================
//  SCREEN 6: SEASONS ORBIT  — full canvas, NOAA-style Southern Hemisphere
// =============================================================================
static void build_seasons_screen() {
    lv_obj_t *scr = screens[SCREEN_SEASONS];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Earth & Seasons - Southern Hemisphere");

    // Full height canvas — no stat cards, everything drawn on canvas
    const int SEA_W = SCREEN_W - NAV_BTN_W * 2;
    const int SEA_H = SCREEN_H - STATUSBAR_H - 26;  // 26 = dot bar

    sea_canvas_buf = (lv_color_t *)heap_caps_malloc(
        SEA_W * SEA_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sea_canvas_buf) sea_canvas_buf = (lv_color_t *)malloc(
        SEA_W * SEA_H * sizeof(lv_color_t));

    sea_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(sea_canvas, sea_canvas_buf, SEA_W, SEA_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(sea_canvas, NAV_BTN_W, STATUSBAR_H);
    lv_canvas_fill_bg(sea_canvas, C_BG, LV_OPA_COVER);

    // ── LVGL label overlays (canvas can't draw text in LVGL8) ─────────────────
    // Season name + progress info labels in quadrant centres
    // ocx=570 ocy=325 orx=433 ory=234 — quadrant centres:
    // Order matches sea_q_dates/sea_q_status: 0=SUMMER, 1=AUTUMN, 2=WINTER, 3=SPRING
    struct { const char *name; lv_color_t col; int x; int y; } slbls[] = {
        { "SUMMER", lv_color_hex(0xFF7B50), 787, 208 },  // top-right
        { "AUTUMN", lv_color_hex(0xD29922), 354, 208 },  // top-left
        { "WINTER", lv_color_hex(0x79C0FF), 354, 442 },  // bot-left
        { "SPRING", lv_color_hex(0x3FB950), 787, 442 },  // bot-right
    };
    for (int qi = 0; qi < 4; qi++) {
        auto &sl = slbls[qi];
        // raw_sy = quadrant centre in screen coords (unshifted)
        int raw_sy = STATUSBAR_H + sl.y;
        int sx     = NAV_BTN_W + sl.x - 30;

        // Season name — at raw_sy - 36 (leaves room for date + bar + status below)
        lv_obj_t *lbl = lv_label_create(scr);
        lv_label_set_text(lbl, sl.name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, sl.col, 0);
        lv_obj_set_pos(lbl, sx, raw_sy - 36);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

        // Date span sub-label — at raw_sy - 12  (6px gap below name)
        sea_q_dates[qi] = lv_label_create(scr);
        lv_label_set_text(sea_q_dates[qi], "--");
        lv_obj_set_style_text_font(sea_q_dates[qi], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sea_q_dates[qi], sl.col, 0);
        lv_obj_set_style_opa(sea_q_dates[qi], LV_OPA_80, 0);
        lv_obj_set_pos(sea_q_dates[qi], sx, raw_sy - 12);
        lv_obj_clear_flag(sea_q_dates[qi], LV_OBJ_FLAG_CLICKABLE);

        // Status label — at raw_sy + 22  (bar is at canvas cy+10 = screen raw_sy+10, 7px tall)
        sea_q_status[qi] = lv_label_create(scr);
        lv_label_set_text(sea_q_status[qi], "--");
        lv_obj_set_style_text_font(sea_q_status[qi], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sea_q_status[qi], C_DIM, 0);
        lv_obj_set_pos(sea_q_status[qi], sx, raw_sy + 22);
        lv_obj_clear_flag(sea_q_status[qi], LV_OBJ_FLAG_CLICKABLE);
    }

    // Month labels outside orbit — skip 0°/90°/180°/270° (covered by event globes)
    const char *mnames[] = {"Dec","Jan","Feb","Sep","Oct","Nov",
                             "Jun","Jul","Aug","Mar","Apr","May"};
    int mangles[] = {0,30,60,90,120,150,180,210,240,270,300,330};
    const int ocx2=570, ocy2=325, orx2=433, ory2=234;
    for (int mi = 0; mi < 12; mi++) {
        if (mangles[mi] == 0 || mangles[mi] == 90 ||
            mangles[mi] == 180 || mangles[mi] == 270) continue;
        float rd = mangles[mi] * (float)M_PI / 180.0f;
        int lx = ocx2 + (int)((orx2 + 32) * cosf(rd));
        int ly = ocy2 + (int)((ory2 + 32) * sinf(rd));
        lv_obj_t *ml = lv_label_create(scr);
        lv_label_set_text(ml, mnames[mi]);
        lv_obj_set_style_text_font(ml, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ml, C_DIM, 0);
        lv_obj_set_pos(ml, NAV_BTN_W + lx - 12, STATUSBAR_H + ly - 8);
        lv_obj_clear_flag(ml, LV_OBJ_FLAG_CLICKABLE);
    }

    // Solstice/Equinox labels at 4 positions
    struct { float angle; const char *line1; const char *line2; lv_color_t col; } evtlbls[] = {
        { 270.0f, "Mar Equinox",  "Autumn begins", lv_color_hex(0xD29922) },
        { 180.0f, "Jun Solstice", "Winter begins", lv_color_hex(0x79C0FF) },
        {  90.0f, "Sep Equinox",  "Spring begins", lv_color_hex(0x3FB950) },
        {   0.0f, "Dec Solstice", "Summer begins", lv_color_hex(0xFF7B50) },
    };
    for (auto &ev : evtlbls) {
        float rd = ev.angle * (float)M_PI / 180.0f;
        int ex = ocx2 + (int)(orx2 * cosf(rd));
        int ey = ocy2 + (int)(ory2 * sinf(rd));
        int ly_off = (ey < ocy2) ? -46 : 24;
        lv_obj_t *l1 = lv_label_create(scr);
        lv_label_set_text(l1, ev.line1);
        lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l1, ev.col, 0);
        lv_obj_set_pos(l1, NAV_BTN_W + ex - 42, STATUSBAR_H + ey + ly_off);
        lv_obj_clear_flag(l1, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *l2 = lv_label_create(scr);
        lv_label_set_text(l2, ev.line2);
        lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l2, ev.col, 0);
        lv_obj_set_pos(l2, NAV_BTN_W + ex - 38, STATUSBAR_H + ey + ly_off + 16);
        lv_obj_clear_flag(l2, LV_OBJ_FLAG_CLICKABLE);
    }

    // Bottom info strip — current season and next event
    sea_season = lv_label_create(scr);
    lv_label_set_text(sea_season, "Season: --");
    lv_obj_set_style_text_font(sea_season, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sea_season, C_DIM, 0);
    lv_obj_align(sea_season, LV_ALIGN_BOTTOM_LEFT, NAV_BTN_W + PAD, -4);

    sea_next_event = lv_label_create(scr);
    lv_label_set_text(sea_next_event, "Next: --");
    lv_obj_set_style_text_font(sea_next_event, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sea_next_event, C_ACCENT, 0);
    lv_obj_align(sea_next_event, LV_ALIGN_BOTTOM_RIGHT, -(NAV_BTN_W + PAD), -4);

    // ── Animated Earth position overlay — two concentric rings around the globe ──
    // Outer ring (sea_earth_dot): 64×64 bright yellow, pulses opacity.
    // Inner ring: 44×44 white, always visible as a static locator ring.
    // Both sit above the canvas so they animate without a full canvas redraw.

    // Inner ring — always-on tight locator around the Earth body (34px = r14+3px gap)
    lv_obj_t *sea_earth_inner = lv_obj_create(scr);
    lv_obj_set_size(sea_earth_inner, 34, 34);
    lv_obj_set_style_bg_opa(sea_earth_inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(sea_earth_inner, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(sea_earth_inner, 2, 0);
    lv_obj_set_style_border_opa(sea_earth_inner, LV_OPA_70, 0);
    lv_obj_set_style_radius(sea_earth_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(sea_earth_inner, 0, 0);
    lv_obj_clear_flag(sea_earth_inner, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(sea_earth_inner, -100, -100);

    // Outer pulsing ring — 50×50 warm yellow, animates to draw the eye
    sea_earth_dot = lv_obj_create(scr);
    lv_obj_set_size(sea_earth_dot, 50, 50);
    lv_obj_set_style_bg_opa(sea_earth_dot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(sea_earth_dot, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_border_width(sea_earth_dot, 3, 0);
    lv_obj_set_style_border_opa(sea_earth_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sea_earth_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(sea_earth_dot, 0, 0);
    lv_obj_clear_flag(sea_earth_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(sea_earth_dot, -100, -100);

    // Pulse timer — 500 ms, 4-step breathe: full → 60% → 20% → 60%
    // Also repositions the inner ring (stored as user_data on the timer's parent obj)
    // We track inner ring via a static pointer captured in the lambda.
    static lv_obj_t *s_inner = nullptr;
    s_inner = sea_earth_inner;
    lv_timer_create([](lv_timer_t *) {
        if (!sea_earth_dot || !s_inner) return;
        static const lv_opa_t steps[] = { LV_OPA_COVER, LV_OPA_60, LV_OPA_20, LV_OPA_60 };
        sea_pulse_phase = (sea_pulse_phase + 1) % 4;
        lv_obj_set_style_border_opa(sea_earth_dot, steps[sea_pulse_phase], 0);
        // Keep inner ring co-located: offset = (50-34)/2 = 8px inward
        lv_coord_t ox = lv_obj_get_x(sea_earth_dot);
        lv_coord_t oy = lv_obj_get_y(sea_earth_dot);
        if (ox > -50)   // only move once dot has been placed by first render
            lv_obj_set_pos(s_inner, ox + 8, oy + 8);
    }, 500, nullptr);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  SCREEN 7: LUNAR ORBIT
// =============================================================================
static void build_lunar_screen() {
    lv_obj_t *scr = screens[SCREEN_LUNAR];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Lunar Orbit");

    // Full-screen canvas — observatory style, labels overlaid
    lun_canvas_buf = (lv_color_t *)heap_caps_malloc(
        CEL_W * CEL_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!lun_canvas_buf) lun_canvas_buf = (lv_color_t *)malloc(
        CEL_W * CEL_H * sizeof(lv_color_t));

    lun_canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(lun_canvas, lun_canvas_buf, CEL_W, CEL_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(lun_canvas, NAV_BTN_W, STATUSBAR_H);
    lv_canvas_fill_bg(lun_canvas, C_BG, LV_OPA_COVER);

    // ── LVGL overlay labels (canvas can't draw text in LVGL8) ─────────────────
    // All positions in screen coords (NAV_BTN_W=70 + canvas_x, STATUSBAR_H=44 + canvas_y)
    // Layout:
    //   Top-left corner:    Moonrise + countdown
    //   Top-right corner:   Moonset  + countdown
    //   Centre top:         Phase name (large) + illumination % + bar
    //   Bottom-left corner: Age label
    //   Bottom-right corner:Days to full / days to new
    //   Centre bottom (above timeline): current phase highlighted

    const int CX = NAV_BTN_W;   // canvas left edge in screen coords
    const int CY = STATUSBAR_H; // canvas top edge in screen coords
    const int CW = CEL_W;       // 1140
    // Canvas centre: screen (CX + CW/2, CY + CEL_H/2) = (640, 342)
    const int SCX = CX + CW / 2;   // screen centre x = 640

    // ── Phase name — large, centred, just inside orbit top ───────────────────
    lun_phase = lv_label_create(scr);
    lv_label_set_text(lun_phase, "—");
    lv_obj_set_style_text_font(lun_phase, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lun_phase, C_TEXT, 0);
    lv_obj_set_style_text_align(lun_phase, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lun_phase, LV_ALIGN_TOP_MID, CX - SCREEN_W/2 + SCX - SCREEN_W/2, CY - STATUSBAR_H + 100);
    // Simpler: absolute position centred on screen
    lv_obj_set_pos(lun_phase, SCX - 120, CY + 88);
    lv_obj_set_width(lun_phase, 240);
    lv_obj_clear_flag(lun_phase, LV_OBJ_FLAG_CLICKABLE);

    // ── Illumination % — below phase name ────────────────────────────────────
    lun_illum = lv_label_create(scr);
    lv_label_set_text(lun_illum, "—");
    lv_obj_set_style_text_font(lun_illum, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lun_illum, C_COOL, 0);
    lv_obj_set_style_text_align(lun_illum, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lun_illum, SCX - 100, CY + 124);
    lv_obj_set_width(lun_illum, 200);
    lv_obj_clear_flag(lun_illum, LV_OBJ_FLAG_CLICKABLE);

    // ── Illumination bar track (200px wide, centred) ──────────────────────────
    lv_obj_t *bar_track = lv_obj_create(scr);
    lv_obj_set_size(bar_track, 200, 6);
    lv_obj_set_pos(bar_track, SCX - 100, CY + 150);
    lv_obj_set_style_bg_color(bar_track, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_bg_opa(bar_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_track, 0, 0);
    lv_obj_set_style_radius(bar_track, 3, 0);
    lv_obj_set_style_pad_all(bar_track, 0, 0);
    lv_obj_clear_flag(bar_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lun_illum_bar = lv_obj_create(scr);
    lv_obj_set_size(lun_illum_bar, 2, 6);   // width updated in render_lunar
    lv_obj_set_pos(lun_illum_bar, SCX - 100, CY + 150);
    lv_obj_set_style_bg_color(lun_illum_bar, C_COOL, 0);
    lv_obj_set_style_bg_opa(lun_illum_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lun_illum_bar, 0, 0);
    lv_obj_set_style_radius(lun_illum_bar, 3, 0);
    lv_obj_set_style_pad_all(lun_illum_bar, 0, 0);
    lv_obj_clear_flag(lun_illum_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Top-left: Moonrise ────────────────────────────────────────────────────
    make_label(scr, "Moonrise", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, CX + 12, CY + 10);
    lun_moonrise = lv_label_create(scr);
    lv_label_set_text(lun_moonrise, "--:--");
    lv_obj_set_style_text_font(lun_moonrise, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lun_moonrise, lv_color_hex(0xCDD9E5), 0);
    lv_obj_set_pos(lun_moonrise, CX + 12, CY + 28);
    lv_obj_clear_flag(lun_moonrise, LV_OBJ_FLAG_CLICKABLE);

    lun_moonrise_sub = lv_label_create(scr);
    lv_label_set_text(lun_moonrise_sub, "--");
    lv_obj_set_style_text_font(lun_moonrise_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lun_moonrise_sub, C_DIM, 0);
    lv_obj_set_pos(lun_moonrise_sub, CX + 12, CY + 52);
    lv_obj_clear_flag(lun_moonrise_sub, LV_OBJ_FLAG_CLICKABLE);

    // ── Top-right: Moonset ────────────────────────────────────────────────────
    make_label(scr, "Moonset", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_RIGHT, -(CX + 12), CY + 10);
    lun_moonset = lv_label_create(scr);
    lv_label_set_text(lun_moonset, "--:--");
    lv_obj_set_style_text_font(lun_moonset, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lun_moonset, C_DIM, 0);
    lv_obj_set_pos(lun_moonset, CX + CW - 110, CY + 28);
    lv_obj_clear_flag(lun_moonset, LV_OBJ_FLAG_CLICKABLE);

    lun_moonset_sub = lv_label_create(scr);
    lv_label_set_text(lun_moonset_sub, "--");
    lv_obj_set_style_text_font(lun_moonset_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lun_moonset_sub, C_DIM, 0);
    lv_obj_set_pos(lun_moonset_sub, CX + CW - 110, CY + 52);
    lv_obj_clear_flag(lun_moonset_sub, LV_OBJ_FLAG_CLICKABLE);

    // ── Mid-left: Lunar age ───────────────────────────────────────────────────
    // Positioned in the lower-left gap between the orbit bottom and the timeline
    // strip. Using absolute coords avoids ALIGN_BOTTOM_* anchoring to screen edge
    // which was placing labels below the canvas (and into the timeline zone).
    // Orbit bottom (screen): CY + CEL_H/2 + CEL_H*0.38 ≈ CY + 524 = 568
    // Timeline disc top (screen): CY + (CEL_H-32) - 14 = 594
    // Mid gap centre ≈ 484-510: use that band for side labels.
    {
        lv_obj_t *la_lbl = lv_label_create(scr);
        lv_label_set_text(la_lbl, "Lunar age");
        lv_obj_set_style_text_font(la_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(la_lbl, C_DIM, 0);
        lv_obj_set_pos(la_lbl, CX + 12, CY + 484);
        lv_obj_clear_flag(la_lbl, LV_OBJ_FLAG_CLICKABLE);
    }
    lun_age = lv_label_create(scr);
    lv_label_set_text(lun_age, "—");
    lv_obj_set_style_text_font(lun_age, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lun_age, C_COOL, 0);
    lv_obj_set_pos(lun_age, CX + 12, CY + 502);
    lv_obj_clear_flag(lun_age, LV_OBJ_FLAG_CLICKABLE);

    // ── Mid-right: Next full / next new ──────────────────────────────────────
    // Same vertical band as age labels — safely above the timeline strip.
    {
        lv_obj_t *nf_lbl = lv_label_create(scr);
        lv_label_set_text(nf_lbl, "Next full");
        lv_obj_set_style_text_font(nf_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(nf_lbl, C_DIM, 0);
        lv_obj_set_pos(nf_lbl, CX + CW - 100, CY + 484);
        lv_obj_clear_flag(nf_lbl, LV_OBJ_FLAG_CLICKABLE);
    }
    lun_full = lv_label_create(scr);
    lv_label_set_text(lun_full, "—");
    lv_obj_set_style_text_font(lun_full, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lun_full, lv_color_hex(0xFFE066), 0);
    lv_obj_set_pos(lun_full, CX + CW - 100, CY + 502);
    lv_obj_clear_flag(lun_full, LV_OBJ_FLAG_CLICKABLE);

    {
        lv_obj_t *nn_lbl = lv_label_create(scr);
        lv_label_set_text(nn_lbl, "Next new");
        lv_obj_set_style_text_font(nn_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(nn_lbl, C_DIM, 0);
        lv_obj_set_pos(nn_lbl, CX + CW - 100, CY + 534);
        lv_obj_clear_flag(nn_lbl, LV_OBJ_FLAG_CLICKABLE);
    }
    lun_new = lv_label_create(scr);
    lv_label_set_text(lun_new, "—");
    lv_obj_set_style_text_font(lun_new, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lun_new, C_DIM, 0);
    lv_obj_set_pos(lun_new, CX + CW - 100, CY + 552);
    lv_obj_clear_flag(lun_new, LV_OBJ_FLAG_CLICKABLE);

    // ── Centre above timeline: next phase ─────────────────────────────────────
    // Moved up from below-timeline (was y=638, clashing with TL labels at 624)
    // Now sits between orbit bottom (568) and TL disc top (594) — just above strip.
    lun_next = lv_label_create(scr);
    lv_label_set_text(lun_next, "—");
    lv_obj_set_style_text_font(lun_next, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lun_next, C_ACCENT, 0);
    lv_obj_set_style_text_align(lun_next, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lun_next, SCX - 120, CY + CEL_H / 2 + 166);  // inside orbit, 60px above orbit bottom
    lv_obj_set_width(lun_next, 240);
    lv_obj_clear_flag(lun_next, LV_OBJ_FLAG_CLICKABLE);

    // ── Phase timeline labels ─────────────────────────────────────────────────
    // 8 short labels below each timeline disc (canvas can't render fonts in LVGL8)
    // Timeline discs at canvas y = CEL_H-32, screen y = CY+(CEL_H-32) = 608
    // TL_R=14 → disc bottom at 622. Labels sit just below at 624.
    // Canvas bottom = CY+CEL_H = 640, screen bottom = 720.
    // Labels (font_14 ~16px) bottom at ~640 — fits within canvas bottom.
    {
        const char *tl_names[] = {
            "New", "Wax C", "1st Q", "Wax G",
            "Full", "Wan G", "Last Q", "Wan C"
        };
        const int TL_SPX    = CEL_W / 9;          // 126
        const int TL_DISC_Y = CY + (CEL_H - 32);  // screen y of disc centre = 608
        const int LBL_Y     = TL_DISC_Y + 16;     // just below disc edge = 624

        for (int ti = 0; ti < 8; ti++) {
            int tx = CX + (ti + 1) * TL_SPX;
            lv_obj_t *tll = lv_label_create(scr);
            lv_label_set_text(tll, tl_names[ti]);
            lv_obj_set_style_text_font(tll, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(tll, C_DIM, 0);
            lv_obj_set_style_text_align(tll, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_size(tll, 52, 16);
            lv_obj_set_pos(tll, tx - 26, LBL_Y);
            lv_obj_clear_flag(tll, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  CELESTIAL CANVAS RENDERERS  (called from ui_update_celestial)
// =============================================================================
// Forward declaration — render_astronomy is defined after ui_update_weather
// but called from both ui_update_weather and ui_update_celestial.
static void render_astronomy(const WeatherResult &wr);

static void render_seasons(const RtcDateTime &dt) {
    if (!sea_canvas) return;

    const int W   = SCREEN_W - NAV_BTN_W * 2;   // 1140
    const int H   = SCREEN_H - STATUSBAR_H - 26; // 650
    lv_canvas_fill_bg(sea_canvas, C_BG, LV_OPA_COVER);

    // ── Orbit geometry ────────────────────────────────────────────────────────
    // Clockwise SH orbit: Dec Sol(right,0°) → Mar Eq(top,270°) →
    //                     Jun Sol(left,180°) → Sep Eq(bottom,90°)
    // SVG angle convention: 0°=right, 90°=bottom, 180°=left, 270°=top
    const int ocx = W / 2;               // 570
    const int ocy = H / 2;               // 325
    const int orx = (int)(W * 0.38f);    // 433
    const int ory = (int)(H * 0.36f);    // 234

    int doy = day_of_year(dt.day, dt.month, dt.year);
    SeasonEvts e = calc_season_events(dt.year);

    // earth_orbit_angle returns degrees in our coordinate system
    float ea = earth_orbit_angle(doy, e);

    // ── Quadrant fills — pixel-by-pixel fill inside orbit ─────────────────────
    // Southern Hemisphere quadrant map:
    //   top-right   (x>cx, y<cy) = SUMMER  orange  (Dec Sol → Mar Eq)
    //   top-left    (x<cx, y<cy) = AUTUMN  amber   (Mar Eq → Jun Sol)
    //   bottom-left (x<cx, y>cy) = WINTER  blue    (Jun Sol → Sep Eq)
    //   bottom-right(x>cx, y>cy) = SPRING  green   (Sep Eq → Dec Sol)
    lv_color_t qCols[4] = {
        lv_color_hex(0xFF7B50),  // top-right    = SUMMER
        lv_color_hex(0xD29922),  // top-left     = AUTUMN
        lv_color_hex(0x1C3A5E),  // bottom-left  = WINTER (dark blue, not bright)
        lv_color_hex(0x1A3A1A),  // bottom-right = SPRING (dark green)
    };
    // Fill at low opacity by drawing with alpha blend approximation
    // Since LVGL canvas pixel ops are solid colour, we draw dim versions
    lv_color_t qColsDim[4] = {
        lv_color_hex(0x3D2010),  // SUMMER dim
        lv_color_hex(0x2D2208),  // AUTUMN dim
        lv_color_hex(0x0A1628),  // WINTER dim
        lv_color_hex(0x0A1A0A),  // SPRING dim
    };

    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            float dx = (float)(px - ocx) / orx;
            float dy = (float)(py - ocy) / ory;
            if (dx*dx + dy*dy < 1.0f) {
                // Inside orbit — determine quadrant
                int qi;
                if      (px >= ocx && py <= ocy) qi = 0;  // top-right
                else if (px <  ocx && py <= ocy) qi = 1;  // top-left
                else if (px <  ocx && py >  ocy) qi = 2;  // bottom-left
                else                              qi = 3;  // bottom-right
                lv_canvas_set_px_color(sea_canvas, px, py, qColsDim[qi]);
            }
        }
    }

    // ── Axis crosshairs (dashed) ──────────────────────────────────────────────
    for (int x = ocx - orx; x <= ocx + orx; x += 9)
        canvas_line(sea_canvas, x, ocy, x+5, ocy, lv_color_hex(0x21262D), 1);
    for (int y = ocy - ory; y <= ocy + ory; y += 9)
        canvas_line(sea_canvas, ocx, y, ocx, y+5, lv_color_hex(0x21262D), 1);

    // ── Orbit ellipse ─────────────────────────────────────────────────────────
    for (int deg = 0; deg < 360; deg++) {
        float r1 = deg       * (float)M_PI / 180.0f;
        float r2 = (deg + 1) * (float)M_PI / 180.0f;
        int x1 = ocx + (int)(orx * cosf(r1)), y1 = ocy + (int)(ory * sinf(r1));
        int x2 = ocx + (int)(orx * cosf(r2)), y2 = ocy + (int)(ory * sinf(r2));
        canvas_line(sea_canvas, x1, y1, x2, y2, C_BORDER, 2);
    }

    // ── Season name labels in quadrant centres ────────────────────────────────
    struct SLabel { const char *name; lv_color_t col; int qx; int qy; };
    SLabel slabels[] = {
        { "SUMMER", lv_color_hex(0xFF7B50), ocx + orx/2, ocy - ory/2 },
        { "AUTUMN", lv_color_hex(0xD29922), ocx - orx/2, ocy - ory/2 },
        { "WINTER", lv_color_hex(0x79C0FF), ocx - orx/2, ocy + ory/2 },
        { "SPRING", lv_color_hex(0x3FB950), ocx + orx/2, ocy + ory/2 },
    };
    // Season names shown via LVGL label overlays created in build_seasons_screen()
    // No canvas dots needed — Earth globes and LVGL labels provide all visual cues

    // ── Month ticks — outside the orbit ──────────────────────────────────────
    // Skip 0°/90°/180°/270° — those positions have Earth globes already
    // Map: Dec=0°, Jan=30°, Feb=60°, Sep=90°, Oct=120°, Nov=150°,
    //      Jun=180°, Jul=210°, Aug=240°, Mar=270°, Apr=300°, May=330°
    int monthAngles[] = { 0,30,60,90,120,150,180,210,240,270,300,330 };
    for (int mi = 0; mi < 12; mi++) {
        int ang = monthAngles[mi];
        if (ang == 0 || ang == 90 || ang == 180 || ang == 270) continue;
        float rd = ang * (float)M_PI / 180.0f;
        int ix  = ocx + (int)(orx        * cosf(rd));
        int iy  = ocy + (int)(ory        * sinf(rd));
        int ox2 = ocx + (int)((orx + 14) * cosf(rd));
        int oy2 = ocy + (int)((ory + 14) * sinf(rd));
        canvas_line(sea_canvas, ix, iy, ox2, oy2, C_DIM, 2);
    }

    // ── Sun at centre ─────────────────────────────────────────────────────────
    draw_sun_icon(sea_canvas, ocx, ocy, 22, lv_color_hex(0xFFB700), C_BG);
    canvas_fill_circle(sea_canvas, ocx, ocy, 14, lv_color_hex(0xFFE066));

    // ── Earth globes at 4 key positions ──────────────────────────────────────
    struct EPos { float angle; lv_color_t col; const char *evtName; const char *season; };
    EPos epos[] = {
        { 270.0f, lv_color_hex(0xD29922), "Mar Eq",  "Autumn" },   // top
        { 180.0f, lv_color_hex(0x79C0FF), "Jun Sol", "Winter" },   // left
        {  90.0f, lv_color_hex(0x3FB950), "Sep Eq",  "Spring" },   // bottom
        {   0.0f, lv_color_hex(0xFF7B50), "Dec Sol", "Summer" },   // right
    };
    for (auto &ep : epos) {
        float rd = ep.angle * (float)M_PI / 180.0f;
        int ex = ocx + (int)(orx * cosf(rd));
        int ey = ocy + (int)(ory * sinf(rd));
        // Glow ring
        canvas_fill_circle(sea_canvas,   ex, ey, 16, ep.col);
        // Earth body
        canvas_fill_circle(sea_canvas,   ex, ey, 12, lv_color_hex(0x1A3A5C));
        // Land mass suggestion (simple oval)
        for (int dy2 = -8; dy2 <= 8; dy2++) {
            int hw = (int)(4.0f * sqrtf(1.0f - (float)(dy2*dy2) / 64.0f));
            if (hw > 0)
                canvas_line(sea_canvas, ex-hw, ey+dy2, ex+hw, ey+dy2,
                            lv_color_hex(0x2D6A30), 1);
        }
        canvas_stroke_circle(sea_canvas, ex, ey, 12, ep.col, 1);
    }

    // ── Next event calculation ────────────────────────────────────────────────
    int dMar = ((e.mar-doy)%365+365)%365; if(!dMar) dMar=365;
    int dJun = ((e.jun-doy)%365+365)%365; if(!dJun) dJun=365;
    int dSep = ((e.sep-doy)%365+365)%365; if(!dSep) dSep=365;
    int dDec = ((e.dec-doy)%365+365)%365; if(!dDec) dDec=365;
    int minD = dMar;
    if (dJun < minD) minD = dJun;
    if (dSep < minD) minD = dSep;
    if (dDec < minD) minD = dDec;

    // Next event angle in our coordinate system
    float nextAngleDeg = (dMar==minD) ? 270.0f :
                         (dJun==minD) ? 180.0f :
                         (dSep==minD) ?  90.0f : 0.0f;

    const char *nextEvtName = (dMar==minD) ? "Mar Equinox"  :
                              (dJun==minD) ? "Jun Solstice" :
                              (dSep==minD) ? "Sep Equinox"  : "Dec Solstice";
    int nextDoy = (dMar==minD)?e.mar:(dJun==minD)?e.jun:(dSep==minD)?e.sep:e.dec;

    // ── Progress arc along orbit from current Earth to next event ─────────────
    // Orbit goes clockwise in angle: ea decreasing (our angle system is CCW maths)
    // We draw from ea stepping towards nextAngleDeg, going the short way clockwise
    {
        float startA = ea;
        float endA   = nextAngleDeg;
        // Normalise to go clockwise (decreasing angle), wrapping through 0
        float sweep = startA - endA;
        if (sweep < 0) sweep += 360.0f;
        if (sweep > 180.0f) sweep = 360.0f - sweep;  // take short path

        for (float aa = 0; aa <= sweep; aa += 0.8f) {
            float rd = (startA - aa) * (float)M_PI / 180.0f;
            int ax = ocx + (int)((orx + 8) * cosf(rd));
            int ay = ocy + (int)((ory + 8) * sinf(rd));
            lv_canvas_set_px_color(sea_canvas, ax,   ay,   C_ACCENT);
            lv_canvas_set_px_color(sea_canvas, ax+1, ay,   C_ACCENT);
            lv_canvas_set_px_color(sea_canvas, ax,   ay+1, C_ACCENT);
        }
    }

    // ── Per-quadrant season progress bars + labels ───────────────────────────
    // Each quadrant shows: date span, a filled progress bar, days elapsed/remaining.
    // Bar drawn on canvas (pixel drawing). Text updated via LVGL label overlays.
    //
    // Season definitions (SH, start/end doy, quadrant index):
    //   0 SUMMER  Dec Sol → Mar Eq  top-right   canvas centre (787, 208)
    //   1 AUTUMN  Mar Eq  → Jun Sol top-left    canvas centre (354, 208)
    //   2 WINTER  Jun Sol → Sep Eq  bottom-left canvas centre (354, 442)
    //   3 SPRING  Sep Eq  → Dec Sol bottom-right canvas centre (787, 442)
    {
        // Bar dimensions (canvas coords)
        const int BAR_W = 160, BAR_H = 5;  // drawn with canvas_line rows — no radius needed
        // Quadrant canvas centres — must match the label positions in build_seasons_screen
        struct QInfo {
            int   cx, cy;          // canvas centre of quadrant
            int   sdoy, edoy;      // start/end doy (edoy may be >365 for Summer wrap)
            lv_color_t col;
        };
        // Summer wraps the year: start=e.dec, end=e.mar+365
        QInfo qi[4] = {
            { 787, 208, e.dec,       e.mar + 365,   lv_color_hex(0xFF7B50) }, // SUMMER
            { 354, 208, e.mar,       e.jun,         lv_color_hex(0xD29922) }, // AUTUMN
            { 354, 442, e.jun,       e.sep,         lv_color_hex(0x79C0FF) }, // WINTER
            { 787, 442, e.sep,       e.dec,         lv_color_hex(0x3FB950) }, // SPRING
        };

        // Normalised doy for Summer wrap comparison
        int doy_n = doy;  // use as-is; Summer uses doy+365 if doy < e.mar

        for (int q = 0; q < 4; q++) {
            int sdoy = qi[q].sdoy;
            int edoy = qi[q].edoy;
            int dur  = edoy - sdoy;
            if (dur <= 0) dur = 1;

            // Elapsed days into this season (handle Summer year-wrap)
            int elapsed;
            if (q == 0) {  // Summer — may span new year
                if (doy >= e.dec)       elapsed = doy - e.dec;
                else if (doy < e.mar)   elapsed = doy + 365 - e.dec;
                else                    elapsed = -((e.dec - doy + 365) % 365);
            } else {
                elapsed = doy - sdoy;
            }

            // Fraction through season (0–1), clamped
            float frac = 0.0f;
            bool active = (elapsed >= 0 && elapsed <= dur);
            if (active) frac = (float)elapsed / (float)dur;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;

            // ── Canvas: bar track + fill — drawn with canvas_line (no lv_canvas_draw_rect
            // to avoid LVGL8 box artefacts from default border/outline/shadow values)
            int bx = qi[q].cx - BAR_W / 2;
            int by = qi[q].cy + 10;  // between date label and status label

            // Track — draw BAR_H horizontal lines in dim colour
            for (int row = 0; row < BAR_H; row++)
                canvas_line(sea_canvas, bx, by + row, bx + BAR_W - 1, by + row,
                            lv_color_hex(0x21262D), 1);

            // Fill — overwrite left portion with season colour
            int fill_w = (int)(frac * BAR_W);
            if (fill_w > 0) {
                for (int row = 0; row < BAR_H; row++)
                    canvas_line(sea_canvas, bx, by + row, bx + fill_w - 1, by + row,
                                qi[q].col, 1);
            }

            // ── LVGL labels ───────────────────────────────────────────────────
            if (!sea_q_dates[q] || !sea_q_status[q]) continue;

            // Date span label — "22 Dec – 20 Mar"
            // Clamp sdoy/edoy to [1,365] for display
            int sdoy_d = ((sdoy - 1) % 365) + 1;
            int edoy_d = ((edoy - 1) % 365) + 1;
            char dbuf[32];
            snprintf(dbuf, sizeof(dbuf), "%s - %s",
                     doy_to_date_str(sdoy_d, dt.year),
                     doy_to_date_str(edoy_d, dt.year));
            lv_label_set_text(sea_q_dates[q], dbuf);

            // Status label
            char sbuf[40];
            if (active) {
                int remaining = dur - elapsed;
                snprintf(sbuf, sizeof(sbuf), "Day %d of %d  |  %d left",
                         elapsed, dur, remaining);
                // Colour the status label: active season brighter
                lv_obj_set_style_text_color(sea_q_status[q],
                    q == 0 ? lv_color_hex(0xFF7B50) :
                    q == 1 ? lv_color_hex(0xD29922) :
                    q == 2 ? lv_color_hex(0x79C0FF) : lv_color_hex(0x3FB950), 0);
            } else {
                // Days until this season starts
                int days_to;
                if (q == 0) days_to = ((e.dec - doy) % 365 + 365) % 365;
                else if (q == 1) days_to = ((e.mar - doy) % 365 + 365) % 365;
                else if (q == 2) days_to = ((e.jun - doy) % 365 + 365) % 365;
                else             days_to = ((e.sep - doy) % 365 + 365) % 365;
                if (days_to == 0) days_to = 365;
                snprintf(sbuf, sizeof(sbuf), "in %d days", days_to);
                lv_obj_set_style_text_color(sea_q_status[q], C_DIM, 0);
            }
            lv_label_set_text(sea_q_status[q], sbuf);
        }
    }

    // ── Current Earth position ────────────────────────────────────────────────
    float erd = ea * (float)M_PI / 180.0f;
    int epx = ocx + (int)(orx * cosf(erd));
    int epy = ocy + (int)(ory * sinf(erd));

    // Glow
    canvas_fill_circle(sea_canvas, epx, epy, 20, lv_color_hex(0x1C3A5E));
    // Earth
    canvas_fill_circle(sea_canvas, epx, epy, 14, lv_color_hex(0x1A3A5C));
    for (int dy2 = -9; dy2 <= 9; dy2++) {
        int hw = (int)(5.0f * sqrtf(1.0f - (float)(dy2*dy2) / 81.0f));
        if (hw > 0)
            canvas_line(sea_canvas, epx-hw, epy+dy2, epx+hw, epy+dy2,
                        lv_color_hex(0x2D6A30), 1);
    }
    canvas_stroke_circle(sea_canvas, epx, epy, 14, C_TEXT, 2);

    // Reposition animated overlay dot — centred on Earth pixel position
    // epx/epy are canvas-relative; add NAV_BTN_W/STATUSBAR_H for screen coords
    if (sea_earth_dot)
        lv_obj_set_pos(sea_earth_dot,
                       NAV_BTN_W + epx - 25,
                       STATUSBAR_H + epy - 25);

    // ── Update stat labels (sea_season, sea_next_event) ───────────────────────
    const char *seasonName = (doy>=e.dec||doy<e.mar) ? "Summer" :
                             (doy<e.jun)              ? "Autumn" :
                             (doy<e.sep)              ? "Winter" : "Spring";

    // Draw bottom info strip on canvas
    // (sea_season / sea_next_event labels were removed — info drawn as pixels
    //  would need font rendering; instead update the overlay labels if present)
    if (sea_season)     lv_label_set_text(sea_season,     seasonName);
    if (sea_next_event) {
        char nbuf[48];
        snprintf(nbuf, sizeof(nbuf), "%s in %d days (%s)",
                 nextEvtName, minD, doy_to_date_str(nextDoy, dt.year));
        lv_label_set_text(sea_next_event, nbuf);
    }
}

static void render_lunar(const WeatherResult &wr) {
    if (!lun_canvas) return;
    lv_canvas_fill_bg(lun_canvas, C_BG, LV_OPA_COVER);

    const int W   = CEL_W, H = CEL_H;
    const int ocx = W / 2, ocy = H / 2;   // 570, 298
    // Elliptical orbit — fills the canvas with room for labels at corners
    const int orx = (int)(W * 0.42f);      // 478
    const int ory = (int)(H * 0.38f);      // 226

    float age   = lunar_age_now();
    // angle: 0=New Moon at top, increases clockwise
    // We map to canvas: New=top (canvas 270deg), Full=bottom (canvas 90deg)
    float angle = (age / 29.53f) * 360.0f;  // 0-360, 0=New, 180=Full
    int   illum = wr.valid && wr.moonIllumPct > 0
                ? wr.moonIllumPct
                : (int)((1.0f - cosf(angle * (float)M_PI / 180.0f)) / 2.0f * 100.0f);

    // Phase classification
    struct PhaseInfo { const char *name; float nextAng; const char *nextName; };
    PhaseInfo pi;
    if      (angle <  45) pi = {"New Moon",        45,  "Waxing Crescent"};
    else if (angle <  90) pi = {"Waxing Crescent",  90, "First Quarter"  };
    else if (angle < 135) pi = {"First Quarter",   135, "Waxing Gibbous" };
    else if (angle < 180) pi = {"Waxing Gibbous",  180, "Full Moon"      };
    else if (angle < 225) pi = {"Full Moon",        225, "Waning Gibbous" };
    else if (angle < 270) pi = {"Waning Gibbous",  270, "Last Quarter"   };
    else if (angle < 315) pi = {"Last Quarter",    315, "Waning Crescent"};
    else                  pi = {"Waning Crescent", 360, "New Moon"       };

    const char *phaseName = (wr.valid && strlen(wr.moonPhase) > 0) ? wr.moonPhase : pi.name;
    int dNext   = (int)((pi.nextAng - angle) / 360.0f * 29.53f) + 1;
    if (dNext < 1) dNext = 1;

    // Days to full / new
    float dToFull = fmodf(180.0f - angle + 360.0f, 360.0f);
    float dToNew  = fmodf(360.0f - angle, 360.0f);
    int dFull = (int)(dToFull / 360.0f * 29.53f) + 1;
    int dNew  = (int)(dToNew  / 360.0f * 29.53f) + 1;
    if (dFull < 1) dFull = 1;
    if (dNew  < 1) dNew  = 1;

    // ── Starfield — deterministic pseudo-random using a simple LCG ────────────
    {
        uint32_t rng = 0xDEADBEEF;
        for (int i = 0; i < 220; i++) {
            rng = rng * 1664525u + 1013904223u;
            int sx = (int)(rng >> 16) % W;
            rng = rng * 1664525u + 1013904223u;
            int sy = (int)(rng >> 16) % H;
            rng = rng * 1664525u + 1013904223u;
            uint8_t bright = 0x18 + (rng >> 24) % 0x28;
            lv_canvas_set_px_color(lun_canvas, sx, sy,
                lv_color_make(bright, bright, bright + 8));
        }
    }

    // ── Orbit ellipse ─────────────────────────────────────────────────────────
    for (int deg = 0; deg < 360; deg++) {
        float r1 = deg       * (float)M_PI / 180.0f;
        float r2 = (deg + 1) * (float)M_PI / 180.0f;
        int x1 = ocx + (int)(orx * cosf(r1)), y1 = ocy + (int)(ory * sinf(r1));
        int x2 = ocx + (int)(orx * cosf(r2)), y2 = ocy + (int)(ory * sinf(r2));
        canvas_line(lun_canvas, x1, y1, x2, y2, C_BORDER, 1);
    }

    // ── Progress arc — New Moon (top) clockwise to current position ───────────
    // canvas angle: New=270deg, angle increases CW → canvas_deg = 270 + angle
    for (float aa = 0; aa <= angle; aa += 0.6f) {
        float rd = (270.0f + aa) * (float)M_PI / 180.0f;
        int ax = ocx + (int)((orx + 7) * cosf(rd));
        int ay = ocy + (int)((ory + 7) * sinf(rd));
        lv_canvas_set_px_color(lun_canvas, ax,   ay,   C_COOL);
        lv_canvas_set_px_color(lun_canvas, ax+1, ay,   C_COOL);
        lv_canvas_set_px_color(lun_canvas, ax,   ay+1, C_COOL);
    }

    // ── 8 phase positions — small moon discs on orbit ─────────────────────────
    // Phase angles (0=New going clockwise), mapped to canvas coords
    // Canvas angle for phase a = 270 + a  (New at top=270)
    struct PhaseTick {
        float phaseAng;    // 0-360 lunation angle
        const char *name;
    };
    PhaseTick pticks[] = {
        {  0, "New"   }, { 45, "Wax C" }, { 90, "1st Q" }, {135, "Wax G"},
        {180, "Full"  }, {225, "Wan G" }, {270, "Last Q" }, {315, "Wan C"},
    };
    const int TICK_R = 16;   // phase disc radius — increased from 11 for legibility
    const int LABEL_DIST = TICK_R + 20;

    for (auto &pt : pticks) {
        float cd = (270.0f + pt.phaseAng) * (float)M_PI / 180.0f;
        int px = ocx + (int)(orx * cosf(cd));
        int py = ocy + (int)(ory * sinf(cd));

        bool isCurrent = fabsf(fmodf(angle - pt.phaseAng + 360.0f, 360.0f)) < 22.5f;
        bool isNext    = fabsf(fmodf(pt.phaseAng - angle + 360.0f, 360.0f)) < 45.0f && !isCurrent;

        // Glow for current phase tick
        if (isCurrent)
            canvas_stroke_circle(lun_canvas, px, py, TICK_R + 5,
                                 lv_color_hex(0x79C0FF), 1);

        // Draw small phase disc
        float tick_age = pt.phaseAng / 360.0f * 29.53f;
        draw_moon_disc(lun_canvas, px, py, TICK_R, tick_age,
                       lv_color_hex(0xBBBBBB), C_BG, lv_color_hex(0x1A1E24));
        canvas_stroke_circle(lun_canvas, px, py, TICK_R,
                             isCurrent ? C_COOL : C_BORDER,
                             isCurrent ? 2 : 1);

        // Tick line from orbit edge outward
        int lx = ocx + (int)((orx + LABEL_DIST) * cosf(cd));
        int ly = ocy + (int)((ory + LABEL_DIST) * sinf(cd));
        canvas_line(lun_canvas, px + (int)((TICK_R+1)*cosf(cd)),
                                py + (int)((TICK_R+1)*sinf(cd)),
                                lx - (int)(8*cosf(cd)),
                                ly - (int)(8*sinf(cd)),
                    isNext ? C_YELLOW : C_BORDER, isNext ? 2 : 1);
    }

    // ── Earth at centre — blue-green globe ────────────────────────────────────
    // Outer glow
    canvas_fill_circle(lun_canvas, ocx, ocy, 20, lv_color_hex(0x0D2A1A));
    // Ocean
    canvas_fill_circle(lun_canvas, ocx, ocy, 16, lv_color_hex(0x1A4A6E));
    // Land patches
    for (int dy = -10; dy <= 10; dy++) {
        int hw = (int)(6.0f * sqrtf(1.0f - (float)(dy*dy) / 100.0f));
        if (hw > 0)
            canvas_line(lun_canvas, ocx-hw, ocy+dy, ocx+hw, ocy+dy,
                        lv_color_hex(0x2D7A30), 1);
    }
    canvas_stroke_circle(lun_canvas, ocx, ocy, 16, lv_color_hex(0x3FB950), 1);
    // Atmosphere halo
    canvas_stroke_circle(lun_canvas, ocx, ocy, 18, lv_color_hex(0x1C3A5E), 1);

    // ── Current Moon — large disc on orbit ────────────────────────────────────
    float moonCd = (270.0f + angle) * (float)M_PI / 180.0f;
    int mx = ocx + (int)(orx * cosf(moonCd));
    int my = ocy + (int)(ory * sinf(moonCd));
    const int MOON_R = 28;

    // Glow
    canvas_fill_circle(lun_canvas, mx, my, MOON_R + 6, lv_color_hex(0x1A1E26));
    // Phase disc
    draw_moon_disc(lun_canvas, mx, my, MOON_R, age,
                   lv_color_hex(0xD8D6CC), C_BG, lv_color_hex(0x1A1E24));
    canvas_stroke_circle(lun_canvas, mx, my, MOON_R, lv_color_hex(0x8B949E), 2);

    // ── Phase timeline strip at bottom of canvas ──────────────────────────────
    // 8 evenly-spaced small discs showing all phases, current highlighted
    const int TL_Y   = H - 32;   // timeline centre y
    const int TL_R   = 14;       // disc radius
    const int TL_SPX = W / 9;    // spacing = 126px
    for (int ti = 0; ti < 8; ti++) {
        int tx = (ti + 1) * TL_SPX;
        float tPhaseAng = pticks[ti].phaseAng;
        float tAge = tPhaseAng / 360.0f * 29.53f;
        bool tCurrent = fabsf(fmodf(angle - tPhaseAng + 360.0f, 360.0f)) < 22.5f;

        // Highlight bar for current
        if (tCurrent) {
            for (int row = -2; row <= 2; row++)
                canvas_line(lun_canvas, tx - TL_R - 4, TL_Y + row,
                            tx + TL_R + 4, TL_Y + row,
                            lv_color_hex(0x1C3A5E), 1);
        }

        draw_moon_disc(lun_canvas, tx, TL_Y, TL_R, tAge,
                       lv_color_hex(0xCDD9E5), C_BG, lv_color_hex(0x1A1E24));
        canvas_stroke_circle(lun_canvas, tx, TL_Y, TL_R,
                             tCurrent ? C_COOL : C_BORDER,
                             tCurrent ? 2 : 1);
    }

    // ── Update LVGL overlay labels ─────────────────────────────────────────────
    char buf[48];

    if (lun_phase) lv_label_set_text(lun_phase, phaseName);

    if (lun_illum) {
        snprintf(buf, sizeof(buf), "%d%% illuminated", illum);
        lv_label_set_text(lun_illum, buf);
    }
    // Illumination bar (200px track)
    if (lun_illum_bar) {
        int bar_w = (int)(illum / 100.0f * 200.0f);
        if (bar_w < 2) bar_w = 2;
        lv_obj_set_width(lun_illum_bar, bar_w);
        // Colour: dim at low illum, bright at full
        lv_color_t bc = illum > 80 ? lv_color_hex(0xFFE066) :
                         illum > 40 ? C_COOL : C_BORDER;
        lv_obj_set_style_bg_color(lun_illum_bar, bc, 0);
    }

    if (lun_age) {
        snprintf(buf, sizeof(buf), "%.1f days", age);
        lv_label_set_text(lun_age, buf);
    }

    if (lun_next) {
        snprintf(buf, sizeof(buf), "Next: %s in %d day%s",
                 pi.nextName, dNext, dNext == 1 ? "" : "s");
        lv_label_set_text(lun_next, buf);
        lv_obj_set_style_text_color(lun_next, C_ACCENT, 0);
    }

    if (lun_full) {
        snprintf(buf, sizeof(buf), "in %d day%s", dFull, dFull == 1 ? "" : "s");
        lv_label_set_text(lun_full, buf);
    }
    if (lun_new) {
        snprintf(buf, sizeof(buf), "in %d day%s", dNew, dNew == 1 ? "" : "s");
        lv_label_set_text(lun_new, buf);
    }

    // Moonrise / Moonset + countdowns
    if (lun_moonrise && wr.valid) {
        lv_label_set_text(lun_moonrise, wr.moonrise);
        time_t now_t = time(nullptr);
        struct tm *lt = localtime(&now_t);
        int curMin = lt->tm_hour * 60 + lt->tm_min;
        int mrMins = parse_time_to_mins(wr.moonrise);
        int msMins = parse_time_to_mins(wr.moonset);
        auto fmt_cd_l = [](int d, char *out, size_t n) {
            int ad = d < 0 ? -d : d;
            const char *dir = d < 0 ? "ago" : "away";
            if (ad < 60) snprintf(out, n, "%dm %s", ad, dir);
            else         snprintf(out, n, "%dh %dm %s", ad/60, ad%60, dir);
        };
        fmt_cd_l(mrMins - curMin, buf, sizeof(buf));
        if (lun_moonrise_sub) lv_label_set_text(lun_moonrise_sub, buf);
        lv_label_set_text(lun_moonset, wr.moonset);
        fmt_cd_l(msMins - curMin, buf, sizeof(buf));
        if (lun_moonset_sub) lv_label_set_text(lun_moonset_sub, buf);
    }
}

// =============================================================================
//  PUBLIC API
// =============================================================================

// =============================================================================
//  BUILD: SPACE WEATHER SCREEN
// =============================================================================
static void build_space_screen() {
    lv_obj_t *scr = screens[SCREEN_SPACE];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Space Weather - Bureau of Meteorology");

    // ── Layout constants ──────────────────────────────────────────────────────
    const int CY     = STATUSBAR_H;
    const int CH     = SCREEN_H - STATUSBAR_H - 26;  // 650
    const int LCOL   = NAV_BTN_W;                    // 70
    const int LCOL_W = 360;
    const int MCOL   = LCOL + LCOL_W + PAD;          // 454
    const int MCOL_W = SW_CHART_W;                   // 440
    const int RCOL   = MCOL + MCOL_W + PAD;          // 918
    const int RCOL_W = SCREEN_W - NAV_BTN_W - RCOL - NAV_BTN_W;  // ~222

    // helper: thin horizontal divider inside a card at relative y
    auto make_div = [](lv_obj_t *parent, int y, int w) {
        lv_obj_t *d = lv_obj_create(parent);
        lv_obj_set_size(d, w, 1);
        lv_obj_set_pos(d, 0, y);
        lv_obj_set_style_bg_color(d, lv_color_hex(0x30363D), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    };
    const int INNER_W = LCOL_W - PAD * 2;

    // ── LEFT COLUMN — Aurora + Mag alert + Geo warning ───────────────────────
    lv_obj_t *alert_card = make_card_nopad(scr, LCOL, CY + PAD, LCOL_W, CH - PAD * 2);
    lv_obj_set_style_pad_all(alert_card, PAD, 0);

    make_label(alert_card, "AURORA STATUS", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, 0, 0);

    sw_alert_badge = lv_label_create(alert_card);
    lv_label_set_text(sw_alert_badge, "QUIET");
    lv_obj_set_style_text_font(sw_alert_badge, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(sw_alert_badge, C_GREEN, 0);
    lv_obj_set_pos(sw_alert_badge, 0, 18);

    sw_alert_band = lv_label_create(alert_card);
    lv_label_set_text(sw_alert_band, "No active notices");
    lv_obj_set_style_text_font(sw_alert_band, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sw_alert_band, C_DIM, 0);
    lv_obj_set_pos(sw_alert_band, 0, 68);

    make_div(alert_card, 94, INNER_W);

    sw_alert_desc = lv_label_create(alert_card);
    lv_label_set_text(sw_alert_desc, "Waiting for data...");
    lv_obj_set_style_text_font(sw_alert_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sw_alert_desc, C_TEXT, 0);
    lv_label_set_long_mode(sw_alert_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sw_alert_desc, INNER_W);
    lv_obj_set_pos(sw_alert_desc, 0, 104);

    // ── Magnetic alert section ────────────────────────────────────────────────
    make_div(alert_card, 196, INNER_W);
    make_label(alert_card, "MAGNETIC ALERT", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, 0, 206);

    sw_mag_status = lv_label_create(alert_card);
    lv_label_set_text(sw_mag_status, "None");
    lv_obj_set_style_text_font(sw_mag_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sw_mag_status, C_GREEN, 0);
    lv_obj_set_pos(sw_mag_status, 0, 224);

    sw_mag_desc = lv_label_create(alert_card);
    lv_label_set_text(sw_mag_desc, "No magnetic disturbance alerts issued.");
    lv_obj_set_style_text_font(sw_mag_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sw_mag_desc, C_DIM, 0);
    lv_label_set_long_mode(sw_mag_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sw_mag_desc, INNER_W);
    lv_obj_set_pos(sw_mag_desc, 0, 246);

    // ── Geo warning section ───────────────────────────────────────────────────
    make_div(alert_card, 330, INNER_W);
    make_label(alert_card, "GEO WARNING", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, 0, 340);

    sw_geo_pill = lv_label_create(alert_card);
    lv_label_set_text(sw_geo_pill, "None");
    lv_obj_set_style_text_font(sw_geo_pill, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sw_geo_pill, C_GREEN, 0);
    lv_obj_set_pos(sw_geo_pill, 0, 358);

    sw_geo_desc = lv_label_create(alert_card);
    lv_label_set_text(sw_geo_desc, "No geomagnetic warnings currently active.");
    lv_obj_set_style_text_font(sw_geo_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sw_geo_desc, C_DIM, 0);
    lv_label_set_long_mode(sw_geo_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sw_geo_desc, INNER_W);
    lv_obj_set_pos(sw_geo_desc, 0, 380);

    // ── CENTRE COLUMN — K-index hero + full-height bar chart ─────────────────
    const int K_HERO_H    = 130;
    const int CHART_CARD_H = CH - K_HERO_H - PAD * 3;

    lv_obj_t *k_card = make_card_nopad(scr, MCOL, CY + PAD, MCOL_W, K_HERO_H);
    lv_obj_set_style_pad_all(k_card, PAD, 0);

    make_label(k_card, "K-INDEX  (Australian Region)",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    sw_k_value = lv_label_create(k_card);
    lv_label_set_text(sw_k_value, "--");
    lv_obj_set_style_text_font(sw_k_value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(sw_k_value, C_DIM, 0);
    lv_obj_align(sw_k_value, LV_ALIGN_TOP_LEFT, 0, 18);

    sw_k_label = lv_label_create(k_card);
    lv_label_set_text(sw_k_label, "-");
    lv_obj_set_style_text_font(sw_k_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sw_k_label, C_DIM, 0);
    lv_obj_align(sw_k_label, LV_ALIGN_TOP_LEFT, 72, 30);

    sw_k_time = lv_label_create(k_card);
    lv_label_set_text(sw_k_time, "Valid: -");
    lv_obj_set_style_text_font(sw_k_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sw_k_time, C_DIM, 0);
    lv_obj_align(sw_k_time, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // K scale legend — top right of hero
    struct { const char *lbl; lv_color_t col; int x; } kscale[] = {
        {"0-2", C_GREEN,  MCOL_W - PAD*2 - 192},
        {"3-4", C_YELLOW, MCOL_W - PAD*2 - 132},
        {"5-6", C_WARM,   MCOL_W - PAD*2 -  72},
        {"7-9", C_RED,    MCOL_W - PAD*2 -  16},
    };
    for (auto &ks : kscale) {
        lv_obj_t *kl = lv_label_create(k_card);
        lv_label_set_text(kl, ks.lbl);
        lv_obj_set_style_text_font(kl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(kl, ks.col, 0);
        lv_obj_align(kl, LV_ALIGN_TOP_LEFT, ks.x, 24);
    }

    // Chart card — taller to use full remaining height
    lv_obj_t *chart_card = make_card_nopad(scr, MCOL,
                                            CY + PAD + K_HERO_H + PAD,
                                            MCOL_W, CHART_CARD_H);

    make_label(chart_card, "24-hour K-index history (Australian region)",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, PAD, PAD / 2);

    sw_canvas_buf = (lv_color_t *)heap_caps_malloc(
        SW_CHART_W * SW_CHART_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sw_canvas_buf) sw_canvas_buf = (lv_color_t *)malloc(
        SW_CHART_W * SW_CHART_H * sizeof(lv_color_t));

    sw_canvas = lv_canvas_create(chart_card);
    lv_canvas_set_buffer(sw_canvas, sw_canvas_buf, SW_CHART_W, SW_CHART_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(sw_canvas, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_canvas_fill_bg(sw_canvas, C_CARD, LV_OPA_COVER);

    // ── RIGHT COLUMN — A-index, Dst, combined status ──────────────────────────
    const int RCARD_H   = (CH - PAD * 2 - PAD * 2) / 3;
    const int RCARD_PAD = PAD;

    // A-index card
    lv_obj_t *a_card = make_card_nopad(scr, RCOL, CY + PAD, RCOL_W, RCARD_H);
    lv_obj_set_style_pad_all(a_card, RCARD_PAD, 0);

    make_label(a_card, "A-INDEX  (daily avg)",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    sw_a_value = lv_label_create(a_card);
    lv_label_set_text(sw_a_value, "--");
    lv_obj_set_style_text_font(sw_a_value, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(sw_a_value, C_DIM, 0);
    lv_obj_align(sw_a_value, LV_ALIGN_TOP_LEFT, 0, 20);

    sw_a_label = lv_label_create(a_card);
    lv_label_set_text(sw_a_label, "-");
    lv_obj_set_style_text_font(sw_a_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sw_a_label, C_DIM, 0);
    lv_obj_align(sw_a_label, LV_ALIGN_TOP_LEFT, 0, 68);

    // A-index bar track
    lv_obj_t *a_track = lv_obj_create(a_card);
    lv_obj_set_size(a_track, RCOL_W - RCARD_PAD * 2, 6);
    lv_obj_align(a_track, LV_ALIGN_BOTTOM_LEFT, 0, -14);
    lv_obj_set_style_bg_color(a_track, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_bg_opa(a_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(a_track, 0, 0);
    lv_obj_set_style_radius(a_track, 3, 0);
    lv_obj_set_style_pad_all(a_track, 0, 0);
    lv_obj_clear_flag(a_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    sw_a_bar = lv_obj_create(a_card);
    lv_obj_set_size(sw_a_bar, 2, 6);
    lv_obj_align(sw_a_bar, LV_ALIGN_BOTTOM_LEFT, 0, -14);
    lv_obj_set_style_bg_color(sw_a_bar, C_GREEN, 0);
    lv_obj_set_style_bg_opa(sw_a_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sw_a_bar, 0, 0);
    lv_obj_set_style_radius(sw_a_bar, 3, 0);
    lv_obj_set_style_pad_all(sw_a_bar, 0, 0);
    lv_obj_clear_flag(sw_a_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    make_label(a_card, "0=quiet  8=unsettled  30=storm",
               &lv_font_montserrat_14, lv_color_hex(0x30363D),
               LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Dst card — with colour-coded threshold legend
    lv_obj_t *dst_card = make_card_nopad(scr, RCOL,
                                          CY + PAD + RCARD_H + PAD,
                                          RCOL_W, RCARD_H);
    lv_obj_set_style_pad_all(dst_card, RCARD_PAD, 0);

    make_label(dst_card, "Dst INDEX  (nT)",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    sw_dst_value = lv_label_create(dst_card);
    lv_label_set_text(sw_dst_value, "--");
    lv_obj_set_style_text_font(sw_dst_value, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(sw_dst_value, C_DIM, 0);
    lv_obj_align(sw_dst_value, LV_ALIGN_TOP_LEFT, 0, 20);

    sw_dst_label = lv_label_create(dst_card);
    lv_label_set_text(sw_dst_label, "-");
    lv_obj_set_style_text_font(sw_dst_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sw_dst_label, C_DIM, 0);
    lv_obj_align(sw_dst_label, LV_ALIGN_TOP_LEFT, 0, 68);

    // Colour-coded Dst threshold legend — absolute positions, no overlap
    // inner h = RCARD_H - PAD*2 = 136px. Rows at 80,94,108,122 (font_14, 14px tall)
    struct { const char *txt; lv_color_t col; int y; } dstscale[] = {
        {"0 = quiet",     C_GREEN,  80 },
        {"-20 = storm",   C_YELLOW, 94 },
        {"-50 = intense", C_WARM,  108 },
        {"-100 = severe", C_RED,   122 },
    };
    for (auto &ds : dstscale) {
        lv_obj_t *dl = lv_label_create(dst_card);
        lv_label_set_text(dl, ds.txt);
        lv_obj_set_style_text_font(dl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(dl, ds.col, 0);
        lv_obj_align(dl, LV_ALIGN_TOP_LEFT, 0, ds.y);
    }

    // Combined geo/mag status + updated timestamp (bottom right card)
    // Height extended to align bottom with left/centre columns (670px screen)
    lv_obj_t *geo_card = make_card_nopad(scr, RCOL,
                                          CY + PAD + (RCARD_H + PAD) * 2,
                                          RCOL_W, CH - PAD * 2 - (RCARD_H + PAD) * 2);
    lv_obj_set_style_pad_all(geo_card, RCARD_PAD, 0);

    make_label(geo_card, "GEO WARNING",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    sw_geo_status = lv_label_create(geo_card);
    lv_label_set_text(sw_geo_status, "None active");
    lv_obj_set_style_text_font(sw_geo_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sw_geo_status, C_GREEN, 0);
    lv_obj_align(sw_geo_status, LV_ALIGN_TOP_LEFT, 0, 20);

    make_label(geo_card, "MAG ALERT",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 56);

    sw_mag_status = lv_label_create(geo_card);
    lv_label_set_text(sw_mag_status, "None active");
    lv_obj_set_style_text_font(sw_mag_status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sw_mag_status, C_GREEN, 0);
    lv_obj_align(sw_mag_status, LV_ALIGN_TOP_LEFT, 0, 74);

    sw_updated = lv_label_create(geo_card);
    lv_label_set_text(sw_updated, "Updated: -");
    lv_obj_set_style_text_font(sw_updated, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sw_updated, C_DIM, 0);
    lv_obj_align(sw_updated, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  UPDATE: SPACE WEATHER — called from loop every 15 minutes
// =============================================================================
void ui_update_space_weather(const SpaceWeatherResult &sw) {
    if (!sw_alert_badge) return;

    char buf[64];

    // ── Alert level badge (left column) ──────────────────────────────────────
    // Colour coding: QUIET=green, OUTLOOK=yellow, WATCH=orange, ALERT=red
    lv_color_t alert_col;
    switch (sw.alertLevel) {
        case SPACE_ALERT:   alert_col = C_RED;    break;
        case SPACE_WATCH:   alert_col = C_WARM;   break;
        case SPACE_OUTLOOK: alert_col = C_YELLOW; break;
        default:            alert_col = C_GREEN;  break;
    }
    lv_label_set_text(sw_alert_badge, space_alert_label(sw.alertLevel));
    lv_obj_set_style_text_color(sw_alert_badge, alert_col, 0);

    // Band + description
    if (sw.auroraAlert) {
        snprintf(buf, sizeof(buf), "Visible at %s latitudes  |  K=%d",
                 sw.auroraAlertBand, sw.auroraAlertK);
        lv_label_set_text(sw_alert_band, buf);
        lv_obj_set_style_text_color(sw_alert_band, alert_col, 0);
        lv_label_set_text(sw_alert_desc, sw.auroraAlertDesc);
    } else if (sw.auroraWatch) {
        snprintf(buf, sizeof(buf), "Watch: %s latitudes  |  K=%d expected",
                 sw.auroraWatchBand, sw.auroraWatchK);
        lv_label_set_text(sw_alert_band, buf);
        lv_obj_set_style_text_color(sw_alert_band, alert_col, 0);
        lv_label_set_text(sw_alert_desc, sw.auroraWatchDesc);
    } else if (sw.auroraOutlook) {
        snprintf(buf, sizeof(buf), "Outlook %s--%s  |  K=%d possible",
                 sw.auroraOutlookStart, sw.auroraOutlookEnd, sw.auroraOutlookK);
        lv_label_set_text(sw_alert_band, buf);
        lv_obj_set_style_text_color(sw_alert_band, alert_col, 0);
        lv_label_set_text(sw_alert_desc, sw.auroraOutlookCause[0]
                           ? sw.auroraOutlookCause : "Enhanced solar activity expected");
    } else {
        lv_label_set_text(sw_alert_band, "No active notices");
        lv_obj_set_style_text_color(sw_alert_band, C_DIM, 0);
        lv_label_set_text(sw_alert_desc,
            "Geomagnetic conditions are currently quiet for the Australian region.");
    }

    // Magnetic alert section (left card)
    if (sw.magAlert) {
        lv_label_set_text(sw_mag_status, "ACTIVE");
        lv_obj_set_style_text_color(sw_mag_status, C_RED, 0);
        lv_label_set_text(sw_mag_desc,
            sw.magAlertDesc[0] ? sw.magAlertDesc
                               : "Magnetic alert active for the Australian region.");
        lv_obj_set_style_text_color(sw_mag_desc, C_TEXT, 0);
    } else {
        lv_label_set_text(sw_mag_status, "None");
        lv_obj_set_style_text_color(sw_mag_status, C_GREEN, 0);
        lv_label_set_text(sw_mag_desc, "No magnetic disturbance alerts issued.");
        lv_obj_set_style_text_color(sw_mag_desc, C_DIM, 0);
    }

    // Geo warning section (left card)
    if (sw.magWarning) {
        lv_label_set_text(sw_geo_pill, "ACTIVE");
        lv_obj_set_style_text_color(sw_geo_pill, C_RED, 0);
        lv_label_set_text(sw_geo_desc,
            sw.magWarningDesc[0] ? sw.magWarningDesc
                                 : "Geomagnetic warning active for the Australian region.");
        lv_obj_set_style_text_color(sw_geo_desc, C_TEXT, 0);
    } else {
        lv_label_set_text(sw_geo_pill, "None");
        lv_obj_set_style_text_color(sw_geo_pill, C_GREEN, 0);
        lv_label_set_text(sw_geo_desc, "No geomagnetic warnings currently active.");
        lv_obj_set_style_text_color(sw_geo_desc, C_DIM, 0);
    }

    // Right card: geo + mag summary
    if (sw_geo_status) {
        lv_label_set_text(sw_geo_status, sw.magWarning ? "WARNING ACTIVE" : "None active");
        lv_obj_set_style_text_color(sw_geo_status, sw.magWarning ? C_RED : C_GREEN, 0);
    }
    if (sw_mag_status) {
        lv_label_set_text(sw_mag_status, sw.magAlert ? "ALERT ACTIVE" : "None active");
        lv_obj_set_style_text_color(sw_mag_status, sw.magAlert ? C_RED : C_GREEN, 0);
    }

    // ── K-index hero (centre column) ──────────────────────────────────────────
    if (sw.kIndex >= 0) {
        snprintf(buf, sizeof(buf), "%d", sw.kIndex);
        lv_label_set_text(sw_k_value, buf);

        lv_color_t kcol = sw.kIndex <= 2 ? C_GREEN :
                          sw.kIndex <= 4 ? C_YELLOW :
                          sw.kIndex <= 6 ? C_WARM   : C_RED;
        lv_obj_set_style_text_color(sw_k_value, kcol, 0);
        lv_label_set_text(sw_k_label, space_k_label(sw.kIndex));
        lv_obj_set_style_text_color(sw_k_label, kcol, 0);

        // Timestamp — show UTC hour only, e.g. "Valid: 03:00 UTC"
        // sw.kTime format: "2025-03-17 03:00:00"
        if (strlen(sw.kTime) >= 16) {
            snprintf(buf, sizeof(buf), "Valid: %.5s UTC", sw.kTime + 11);
            lv_label_set_text(sw_k_time, buf);
        }
    } else {
        lv_label_set_text(sw_k_value, "--");
        lv_label_set_text(sw_k_label, "No data");
    }

    // ── K-index history bar chart (canvas) ───────────────────────────────────
    if (sw_canvas && sw_canvas_buf) {
        lv_obj_clean(sw_canvas);
        lv_canvas_set_buffer(sw_canvas, sw_canvas_buf,
                             SW_CHART_W, SW_CHART_H, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(sw_canvas, C_CARD, LV_OPA_COVER);

        const int W = SW_CHART_W, H = SW_CHART_H;
        const int PAD_L = 32, PAD_R = 8, PAD_T = 8, PAD_B = 24;
        const int CW = W - PAD_L - PAD_R;
        const int CH = H - PAD_T - PAD_B;

        // Y axis rail + grid lines (0-9 scale, 3 lines at 3, 6, 9)
        canvas_line(sw_canvas, PAD_L, PAD_T, PAD_L, H - PAD_B, C_BORDER, 1);
        canvas_line(sw_canvas, PAD_L, H - PAD_B, W - PAD_R, H - PAD_B, C_BORDER, 1);
        for (int gk : {3, 6, 9}) {
            int gy = PAD_T + (int)((9 - gk) / 9.0f * CH);
            canvas_line(sw_canvas, PAD_L, gy, W - PAD_R, gy,
                        lv_color_hex(0x21262D), 1);
            canvas_line(sw_canvas, PAD_L - 4, gy, PAD_L, gy, C_DIM, 1);
            // Y label
            lv_obj_t *yl = lv_label_create(sw_canvas);
            char ylbl[4]; snprintf(ylbl, sizeof(ylbl), "%d", gk);
            lv_label_set_text(yl, ylbl);
            lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(yl, C_DIM, 0);
            lv_obj_set_style_bg_opa(yl, LV_OPA_TRANSP, 0);
            lv_obj_set_size(yl, PAD_L - 2, 16);
            lv_obj_set_pos(yl, 0, gy - 8);
        }

        // Count valid history slots
        int validSlots = 0;
        for (int i = 0; i < KINDEX_HISTORY; i++)
            if (sw.kHistory[i] >= 0) validSlots++;

        if (validSlots > 0) {
            const int BAR_GAP = 4;
            const int BAR_W_MAX = CW / KINDEX_HISTORY - BAR_GAP;

            for (int i = 0; i < KINDEX_HISTORY; i++) {
                if (sw.kHistory[i] < 0) continue;

                int k = sw.kHistory[i];
                if (k > 9) k = 9;

                // Bar position
                int bx = PAD_L + (int)((float)i / KINDEX_HISTORY * CW) + BAR_GAP/2;
                int bar_h = (int)((float)k / 9.0f * CH);
                int by = H - PAD_B - bar_h;

                // Bar colour by K value
                lv_color_t bc = k <= 2 ? C_GREEN :
                                k <= 4 ? C_YELLOW :
                                k <= 6 ? C_WARM   : C_RED;

                // Draw bar as horizontal lines (avoid lv_canvas_draw_rect artefacts)
                for (int row = 0; row < bar_h; row++)
                    canvas_line(sw_canvas, bx, by + row,
                                bx + BAR_W_MAX - 1, by + row, bc, 1);

                // Highlight last (most recent) bar
                if (i == KINDEX_HISTORY - 1) {
                    canvas_stroke_circle(sw_canvas, bx + BAR_W_MAX/2, by - 4,
                                         3, C_TEXT, 1);
                }

                // X-axis tick
                canvas_line(sw_canvas, bx + BAR_W_MAX/2, H - PAD_B,
                            bx + BAR_W_MAX/2, H - PAD_B + 4, C_BORDER, 1);
            }

            // X axis labels: "18h ago", "15h ago" ... "now"
            // Each slot = 3h, 8 slots = 24h
            for (int i = 0; i < KINDEX_HISTORY; i++) {
                if (sw.kHistory[i] < 0) continue;
                int hoursAgo = (KINDEX_HISTORY - 1 - i) * 3;
                char xl[8];
                if (hoursAgo == 0) snprintf(xl, sizeof(xl), "now");
                else               snprintf(xl, sizeof(xl), "%dh", hoursAgo);

                int bx = PAD_L + (int)((float)i / KINDEX_HISTORY * CW);
                lv_obj_t *xll = lv_label_create(sw_canvas);
                lv_label_set_text(xll, xl);
                lv_obj_set_style_text_font(xll, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(xll, C_DIM, 0);
                lv_obj_set_style_bg_opa(xll, LV_OPA_TRANSP, 0);
                lv_obj_set_size(xll, 36, 16);
                lv_obj_set_pos(xll, bx + 2, H - PAD_B + 5);
            }
        }
    }

    // ── A-index (right column) ────────────────────────────────────────────────
    if (sw.aIndex >= 0) {
        snprintf(buf, sizeof(buf), "%d", sw.aIndex);
        lv_label_set_text(sw_a_value, buf);

        lv_color_t acol = sw.aIndex < 8  ? C_GREEN  :
                          sw.aIndex < 16 ? C_YELLOW :
                          sw.aIndex < 30 ? C_WARM   : C_RED;
        lv_obj_set_style_text_color(sw_a_value, acol, 0);
        lv_label_set_text(sw_a_label, space_a_label(sw.aIndex));
        lv_obj_set_style_text_color(sw_a_label, acol, 0);

        // A bar — track is 0-100 range (clamped)
        if (sw_a_bar) {
            lv_obj_t *track = lv_obj_get_parent(sw_a_bar);
            int tw = lv_obj_get_width(track);
            // Actually sw_a_bar and a_track are siblings in a_card
            // Get card inner width
            int bar_max_w = 200; // safe fixed width
            int bar_w = (int)(fminf((float)sw.aIndex / 100.0f, 1.0f) * bar_max_w);
            if (bar_w < 2) bar_w = 2;
            lv_obj_set_width(sw_a_bar, bar_w);
            lv_obj_set_style_bg_color(sw_a_bar, acol, 0);
        }
    }

    // ── Dst index (right column) ──────────────────────────────────────────────
    {
        snprintf(buf, sizeof(buf), "%d", sw.dstIndex);
        lv_label_set_text(sw_dst_value, buf);

        lv_color_t dcol = sw.dstIndex > -20  ? C_GREEN  :
                          sw.dstIndex > -50  ? C_YELLOW :
                          sw.dstIndex > -100 ? C_WARM   : C_RED;
        lv_obj_set_style_text_color(sw_dst_value, dcol, 0);
        lv_label_set_text(sw_dst_label, space_dst_label(sw.dstIndex));
        lv_obj_set_style_text_color(sw_dst_label, dcol, 0);
    }

    // ── Updated timestamp ─────────────────────────────────────────────────────
    {
        time_t now4 = time(nullptr);
        struct tm *t4 = localtime(&now4);
        snprintf(buf, sizeof(buf), "Updated %02d:%02d", t4->tm_hour, t4->tm_min);
        lv_label_set_text(sw_updated, buf);
    }
}

// =============================================================================
//  SCREEN 7 — Weather Alerts / Warnings  (WeatherAPI alerts=yes)
// =============================================================================

// Severity colour mapping
static lv_color_t alert_severity_colour(const char *sev) {
    if (!sev || !sev[0])           return lv_color_hex(0x8B949E);  // C_DIM — unknown
    if (strstr(sev, "Extreme"))    return lv_color_hex(0xF85149);  // C_RED
    if (strstr(sev, "Severe"))     return lv_color_hex(0xFF7B50);  // C_WARM orange
    if (strstr(sev, "Moderate"))   return lv_color_hex(0xD29922);  // C_YELLOW
    if (strstr(sev, "Minor"))      return lv_color_hex(0x3FB950);  // C_GREEN
    return lv_color_hex(0x58A6FF);                                  // C_ACCENT blue
}

// Widgets — one card per alert slot
#define ALERTS_MAX_CARDS  5
static lv_obj_t *alert_cards[ALERTS_MAX_CARDS]     = {};
static lv_obj_t *alert_sev_bar[ALERTS_MAX_CARDS]   = {};  // left colour bar
static lv_obj_t *alert_headline[ALERTS_MAX_CARDS]  = {};
static lv_obj_t *alert_meta[ALERTS_MAX_CARDS]      = {};  // severity + areas
static lv_obj_t *alert_time[ALERTS_MAX_CARDS]      = {};  // onset→expires
static lv_obj_t *alert_desc[ALERTS_MAX_CARDS]      = {};  // description text
static lv_obj_t *alerts_no_warn_lbl                = nullptr;
static lv_obj_t *alerts_updated_lbl               = nullptr;
static lv_obj_t *alerts_source_lbl                = nullptr;  // "BOM" / "WeatherAPI"

// =============================================================================
//  BUILD: PLANETS TONIGHT SCREEN
// =============================================================================
static void build_planets_screen() {
    lv_obj_t *scr = screens[SCREEN_PLANETS];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Planets Tonight - Sydney");

    const int CY      = STATUSBAR_H;
    const int CH_FULL = SCREEN_H - STATUSBAR_H - 26;  // 650
    const int LEFT_W  = 460;
    const int LEFT_X  = NAV_BTN_W;
    const int RIGHT_X = LEFT_X + LEFT_W + PAD;
    const int RIGHT_W = SCREEN_W - 2 * NAV_BTN_W - LEFT_W - PAD;  // 656
    const int CARD_H  = CH_FULL - PAD * 2;   // 602

    // ── LEFT CARD — planet visibility list ───────────────────────────────────
    lv_obj_t *list_card = make_card_nopad(scr, LEFT_X, CY + PAD, LEFT_W, CARD_H);
    lv_obj_set_style_pad_all(list_card, PAD, 0);

    make_label(list_card, "VISIBILITY  (altitude | azimuth | magnitude)",
               &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    // Each row: name | pill | bar | alt | az | mag | constellation
    const int INNER_W = LEFT_W - PAD * 2;  // 412px
    const int ROW_H   = (CARD_H - PAD * 2 - 22) / ASTRO_PLANET_COUNT;  // ~82px

    // Column x positions within the card (relative to card pad)
    // Line 1: Name | bar | altitude | azimuth | magnitude
    // Line 2: Pill (Visible/Low/Below) + constellation
    const int COL_NAME = 0;
    const int COL_BAR  = 85;
    const int COL_ALT  = COL_BAR + 150;  // 235
    const int COL_AZ   = COL_ALT + 55;   // 290
    const int COL_MAG  = COL_AZ  + 72;   // 362  (ends at 362+50=412 = INNER_W exactly)

    for (int i = 0; i < ASTRO_PLANET_COUNT; i++) {
        int ry = 22 + i * ROW_H;

        // Divider above each row (not first)
        if (i > 0) {
            lv_obj_t *div = lv_obj_create(list_card);
            lv_obj_set_size(div, INNER_W, 1);
            lv_obj_set_pos(div, 0, ry - 4);
            lv_obj_set_style_bg_color(div, lv_color_hex(0x1C2028), 0);
            lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(div, 0, 0);
            lv_obj_set_style_pad_all(div, 0, 0);
            lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }

        // Line 1: Planet name (font_16, bold colour)
        pl_name[i] = lv_label_create(list_card);
        lv_label_set_text(pl_name[i], "---");
        lv_obj_set_style_text_font(pl_name[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pl_name[i], C_TEXT, 0);
        lv_obj_set_pos(pl_name[i], COL_NAME, ry + 2);

        // Line 2: Visibility pill + constellation (e.g. "Visible  Gemini")
        // pl_pill holds the combined text; pl_con is unused but kept as nullptr guard
        pl_pill[i] = lv_label_create(list_card);
        lv_label_set_text(pl_pill[i], "---");
        lv_obj_set_style_text_font(pl_pill[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pl_pill[i], C_DIM, 0);
        lv_obj_set_width(pl_pill[i], COL_BAR + 145);  // spans name+bar columns
        lv_label_set_long_mode(pl_pill[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(pl_pill[i], COL_NAME, ry + 22);
        pl_con[i] = nullptr;   // not used as separate widget

        // Altitude bar track (sits behind fill bar, centred vertically in row)
        lv_obj_t *bar_track = lv_obj_create(list_card);
        lv_obj_set_size(bar_track, 140, 5);
        lv_obj_set_pos(bar_track, COL_BAR, ry + ROW_H / 2 + 6);
        lv_obj_set_style_bg_color(bar_track, lv_color_hex(0x1C2028), 0);
        lv_obj_set_style_bg_opa(bar_track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar_track, 0, 0);
        lv_obj_set_style_radius(bar_track, 3, 0);
        lv_obj_set_style_pad_all(bar_track, 0, 0);
        lv_obj_clear_flag(bar_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        pl_bar[i] = lv_obj_create(list_card);
        lv_obj_set_size(pl_bar[i], 2, 5);
        lv_obj_set_pos(pl_bar[i], COL_BAR, ry + ROW_H / 2 + 6);
        lv_obj_set_style_bg_color(pl_bar[i], C_GREEN, 0);
        lv_obj_set_style_bg_opa(pl_bar[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(pl_bar[i], 0, 0);
        lv_obj_set_style_radius(pl_bar[i], 3, 0);
        lv_obj_set_style_pad_all(pl_bar[i], 0, 0);
        lv_obj_clear_flag(pl_bar[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // Altitude value (line 1, right of bar)
        pl_alt[i] = lv_label_create(list_card);
        lv_label_set_text(pl_alt[i], "--");
        lv_obj_set_style_text_font(pl_alt[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pl_alt[i], C_DIM, 0);
        lv_obj_set_pos(pl_alt[i], COL_ALT, ry + 2);

        // Azimuth (line 2, below altitude)
        pl_az[i] = lv_label_create(list_card);
        lv_label_set_text(pl_az[i], "--");
        lv_obj_set_style_text_font(pl_az[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pl_az[i], C_DIM, 0);
        lv_obj_set_pos(pl_az[i], COL_ALT, ry + 22);

        // Magnitude (line 1, rightmost — 50px fits "m-3.9")
        pl_mag[i] = lv_label_create(list_card);
        lv_label_set_text(pl_mag[i], "--");
        lv_obj_set_style_text_font(pl_mag[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pl_mag[i], C_DIM, 0);
        lv_obj_set_width(pl_mag[i], 50);
        lv_label_set_long_mode(pl_mag[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_pos(pl_mag[i], COL_MAG, ry + 2);
    }

    // ── RIGHT — compass canvas + best planet summary ──────────────────────────
    // Compass card (fills most of right column height)
    const int SUMMARY_H      = 120;
    const int COMPASS_CARD_H = CARD_H - SUMMARY_H - PAD;
    lv_obj_t *compass_card = make_card_nopad(scr, RIGHT_X, CY + PAD,
                                              RIGHT_W, COMPASS_CARD_H);

    make_label(compass_card, "Sky compass - azimuth / altitude",
               &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, PAD, PAD / 2);

    // Allocate compass canvas in PSRAM
    pl_compass_buf = (lv_color_t *)heap_caps_malloc(
        PL_COMPASS_W * PL_COMPASS_H * sizeof(lv_color_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pl_compass_buf) pl_compass_buf = (lv_color_t *)malloc(
        PL_COMPASS_W * PL_COMPASS_H * sizeof(lv_color_t));

    pl_compass_canvas = lv_canvas_create(compass_card);
    lv_canvas_set_buffer(pl_compass_canvas, pl_compass_buf,
                         PL_COMPASS_W, PL_COMPASS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(pl_compass_canvas, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_canvas_fill_bg(pl_compass_canvas, C_CARD, LV_OPA_COVER);

    // Summary card (bottom right)
    lv_obj_t *sum_card = make_card_nopad(scr, RIGHT_X,
                                          CY + PAD + COMPASS_CARD_H + PAD,
                                          RIGHT_W, SUMMARY_H);
    lv_obj_set_style_pad_all(sum_card, PAD, 0);

    make_label(sum_card, "BEST TONIGHT", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, 0, 0);

    pl_best = lv_label_create(sum_card);
    lv_label_set_text(pl_best, "Waiting for data...");
    lv_obj_set_style_text_font(pl_best, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pl_best, lv_color_hex(0xFFD033), 0);
    lv_obj_align(pl_best, LV_ALIGN_TOP_LEFT, 0, 18);

    pl_best_sub = lv_label_create(sum_card);
    lv_label_set_text(pl_best_sub, "");
    lv_obj_set_style_text_font(pl_best_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pl_best_sub, C_DIM, 0);
    lv_obj_align(pl_best_sub, LV_ALIGN_TOP_LEFT, 0, 42);

    pl_updated = lv_label_create(sum_card);
    lv_label_set_text(pl_updated, "Not yet fetched");
    lv_obj_set_style_text_font(pl_updated, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pl_updated, C_BORDER, 0);
    lv_obj_align(pl_updated, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// ── Compass rendering helper ──────────────────────────────────────────────────
static void render_planet_compass(const AstroResult &ar) {
    if (!pl_compass_canvas || !pl_compass_buf) return;

    lv_obj_clean(pl_compass_canvas);
    lv_canvas_set_buffer(pl_compass_canvas, pl_compass_buf,
                         PL_COMPASS_W, PL_COMPASS_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(pl_compass_canvas, C_CARD, LV_OPA_COVER);

    const int W = PL_COMPASS_W, H = PL_COMPASS_H;
    const int cx = W / 2, cy = H / 2;
    const int R  = (H < W ? H : W) / 2 - 28;  // radius with label clearance

    // Concentric rings (horizon=R, 30°=R*2/3, 60°=R/3)
    lv_color_t ring_col = lv_color_hex(0x2D3A4A);   // noticeably brighter than card bg
    canvas_stroke_circle(pl_compass_canvas, cx, cy, R,      ring_col, 2);
    canvas_stroke_circle(pl_compass_canvas, cx, cy, R*2/3,  ring_col, 1);
    canvas_stroke_circle(pl_compass_canvas, cx, cy, R/3,    ring_col, 1);

    // Cardinal direction labels and tick marks
    struct { float ang; const char *lbl; } cards[] = {
        {270, "N"}, {315, "NE"}, {0, "E"}, {45, "SE"},
        {90, "S"},  {135,"SW"},  {180,"W"},{225,"NW"}
    };
    for (auto &c : cards) {
        float rad = (c.ang - 90.0f) * (float)M_PI / 180.0f;
        int lx = cx + (int)((R + 16) * cosf(rad));
        int ly = cy + (int)((R + 16) * sinf(rad));
        // Tick
        canvas_line(pl_compass_canvas,
                    cx + (int)((R-4)*cosf(rad)), cy + (int)((R-4)*sinf(rad)),
                    cx + (int)( R   *cosf(rad)), cy + (int)( R   *sinf(rad)),
                    lv_color_hex(0x30363D), 1);
        // Label via LVGL overlay
        lv_obj_t *cl = lv_label_create(pl_compass_canvas);
        lv_label_set_text(cl, c.lbl);
        lv_obj_set_style_text_font(cl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(cl,
            strcmp(c.lbl, "N") == 0 ? C_RED : C_BORDER, 0);
        lv_obj_set_style_bg_opa(cl, LV_OPA_TRANSP, 0);
        int lw = strlen(c.lbl) * 8;
        lv_obj_set_pos(cl, lx - lw/2, ly - 8);
    }

    // Ring elevation labels — position at right edge of each ring
    lv_color_t elev_col = lv_color_hex(0x30363D);
    {
        // 30° ring = R*2/3 radius — label at right side
        lv_obj_t *el30 = lv_label_create(pl_compass_canvas);
        lv_label_set_text(el30, "30");
        lv_obj_set_style_text_font(el30, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(el30, elev_col, 0);
        lv_obj_set_style_bg_opa(el30, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(el30, cx + R*2/3 + 4, cy - 8);

        // 60° ring = R/3 radius — label at right side
        lv_obj_t *el60 = lv_label_create(pl_compass_canvas);
        lv_label_set_text(el60, "60");
        lv_obj_set_style_text_font(el60, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(el60, elev_col, 0);
        lv_obj_set_style_bg_opa(el60, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(el60, cx + R/3 + 4, cy - 8);
    }

    // Planet colours
    lv_color_t pcols[] = {
        C_DIM,                    // Mercury — grey
        lv_color_hex(0xE8D5A0),   // Venus — cream white
        C_WARM,                   // Mars — orange
        C_YELLOW,                 // Jupiter — yellow
        lv_color_hex(0x79C0FF),   // Saturn — pale blue
        lv_color_hex(0x58A6FF),   // Uranus — blue
        lv_color_hex(0x4488CC),   // Neptune — deep blue
    };

    // Plot planets
    for (int i = 0; i < ar.count; i++) {
        const PlanetInfo &p = ar.planets[i];
        if (p.altitudeDeg < -5.0f) continue;  // well below horizon — skip

        float ang_rad = (p.azimuthDeg - 90.0f) * (float)M_PI / 180.0f;
        // Radial distance: alt=0°→edge (R), alt=90°→centre (0)
        float alt_clamped = p.altitudeDeg < 0 ? 0 : p.altitudeDeg;
        float rad_frac    = 1.0f - alt_clamped / 90.0f;
        int   px = cx + (int)(R * rad_frac * cosf(ang_rad));
        int   py = cy + (int)(R * rad_frac * sinf(ang_rad));

        // Find colour by matching original planet order
        lv_color_t col = C_TEXT;
        const char *pids[] = {"mercury","venus","mars","jupiter","saturn","uranus","neptune"};
        for (int pi = 0; pi < ASTRO_PLANET_COUNT; pi++) {
            if (strcmp(p.id, pids[pi]) == 0) { col = pcols[pi]; break; }
        }

        // Glow halo for bright planets (magnitude < 0)
        if (p.magnitude < 0.0f)
            canvas_fill_circle(pl_compass_canvas, px, py, 8, lv_color_mix(col, C_BG, 40));

        // Dot — size by brightness
        int dot_r = p.magnitude < -2.0f ? 5 : p.magnitude < 0.0f ? 4 : 3;
        canvas_fill_circle(pl_compass_canvas, px, py, dot_r, col);

        // Name label
        lv_obj_t *nl = lv_label_create(pl_compass_canvas);
        lv_label_set_text(nl, p.name);
        lv_obj_set_style_text_font(nl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(nl, col, 0);
        lv_obj_set_style_bg_opa(nl, LV_OPA_TRANSP, 0);
        int nlw = strlen(p.name) * 8;
        int nlx = px + (px > cx ? dot_r + 3 : -(nlw + dot_r + 1));
        int nly = py - 8;
        if (nlx < 0) nlx = 0;
        if (nlx + nlw > W) nlx = W - nlw;
        lv_obj_set_pos(nl, nlx, nly);
    }

    // Plot Moon if above horizon
    if (ar.moonAltDeg > -5.0f) {
        float ang_rad  = (ar.moonAzDeg - 90.0f) * (float)M_PI / 180.0f;
        float alt_c    = ar.moonAltDeg < 0 ? 0 : ar.moonAltDeg;
        float rad_frac = 1.0f - alt_c / 90.0f;
        int mx = cx + (int)(R * rad_frac * cosf(ang_rad));
        int my = cy + (int)(R * rad_frac * sinf(ang_rad));
        lv_color_t moon_col = lv_color_hex(0xCDD9E5);
        canvas_fill_circle(pl_compass_canvas, mx, my, 5, moon_col);
        lv_obj_t *ml = lv_label_create(pl_compass_canvas);
        lv_label_set_text(ml, "Moon");
        lv_obj_set_style_text_font(ml, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ml, moon_col, 0);
        lv_obj_set_style_bg_opa(ml, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(ml, mx + (mx > cx ? 8 : -40), my - 8);
    }

    // Centre crosshair (zenith)
    canvas_line(pl_compass_canvas, cx-6, cy, cx+6, cy, lv_color_hex(0x21262D), 1);
    canvas_line(pl_compass_canvas, cx, cy-6, cx, cy+6, lv_color_hex(0x21262D), 1);

    lv_obj_invalidate(pl_compass_canvas);
}

// ── Helper: azimuth degrees to compass direction string ───────────────────────
static const char *az_to_dir(float az) {
    int idx = (int)((az + 22.5f) / 45.0f) % 8;
    const char *dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
    return dirs[idx];
}

// =============================================================================
//  UPDATE: PLANETS — called from loop() after astro_fetch()
// =============================================================================
void ui_update_planets(const AstroResult &ar) {
    if (!pl_name[0]) return;

    char buf[48];

    if (!ar.valid) {
        lv_label_set_text(pl_best, "No data");
        lv_label_set_text(pl_best_sub, "Fetch failed - check WiFi");
        return;
    }

    // Update planet rows
    for (int i = 0; i < ASTRO_PLANET_COUNT; i++) {
        if (i >= ar.count) {
            // Hide unused rows
            lv_label_set_text(pl_name[i], "");
            lv_label_set_text(pl_pill[i], "");
            lv_label_set_text(pl_alt[i],  "");
            lv_label_set_text(pl_az[i],   "");
            lv_label_set_text(pl_mag[i],  "");
            // pl_con[i] is nullptr — skip
            continue;
        }

        const PlanetInfo &p = ar.planets[i];

        // Name (colour by broad type)
        lv_label_set_text(pl_name[i], p.name);
        lv_color_t name_col = p.altitudeDeg >= 10.0f ? C_TEXT :
                               p.altitudeDeg >= 1.0f  ? C_DIM  : lv_color_hex(0x444D56);
        lv_obj_set_style_text_color(pl_name[i], name_col, 0);

        // Visibility pill + constellation combined on line 2
        char pill_buf[40];
        const char *vis_str;
        lv_color_t pill_col;
        if (p.altitudeDeg >= 10.0f) {
            vis_str  = "Visible";
            pill_col = C_GREEN;
        } else if (p.altitudeDeg >= 1.0f) {
            vis_str  = "Low";
            pill_col = C_YELLOW;
        } else {
            vis_str  = "Below horizon";
            pill_col = C_BORDER;
        }
        if (p.constellation[0])
            snprintf(pill_buf, sizeof(pill_buf), "%s  %s", vis_str, p.constellation);
        else
            snprintf(pill_buf, sizeof(pill_buf), "%s", vis_str);
        lv_label_set_text(pl_pill[i], pill_buf);
        lv_obj_set_style_text_color(pl_pill[i], pill_col, 0);

        // Altitude bar — map 0-90° to bar width 0-140px
        float alt_clamped = p.altitudeDeg < 0 ? 0 : p.altitudeDeg > 90 ? 90 : p.altitudeDeg;
        int bar_w = (int)(alt_clamped / 90.0f * 140.0f);
        if (bar_w < 2) bar_w = 2;
        lv_obj_set_width(pl_bar[i], bar_w);
        lv_color_t bar_col = p.altitudeDeg >= 10.0f ? C_GREEN :
                              p.altitudeDeg >= 1.0f  ? C_YELLOW : C_BORDER;
        lv_obj_set_style_bg_color(pl_bar[i], bar_col, 0);

        // Altitude value
        snprintf(buf, sizeof(buf), "%+.0f\xC2\xB0", p.altitudeDeg);
        lv_label_set_text(pl_alt[i], buf);
        lv_obj_set_style_text_color(pl_alt[i], bar_col, 0);

        // Azimuth + direction
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0 %s", p.azimuthDeg, az_to_dir(p.azimuthDeg));
        lv_label_set_text(pl_az[i], buf);

        // Magnitude — only colour-code by brightness if actually visible
        snprintf(buf, sizeof(buf), "m%+.1f", p.magnitude);
        lv_label_set_text(pl_mag[i], buf);
        lv_color_t mag_col;
        if (p.altitudeDeg < 1.0f) {
            mag_col = C_BORDER;   // below horizon — grey regardless of brightness
        } else {
            mag_col = p.magnitude < -1.0f ? C_YELLOW :
                      p.magnitude <  1.0f ? C_TEXT   : C_DIM;
        }
        lv_obj_set_style_text_color(pl_mag[i], mag_col, 0);

        // Constellation
        // pl_con[i] is nullptr — constellation shown via pl_pill
    }

    // Best tonight summary — find highest visible planet(s)
    {
        char best[48] = "None visible";
        char sub[64]  = "All planets below horizon tonight";
        int  vis_count = 0;
        const char *top1 = nullptr, *top2 = nullptr;
        for (int i = 0; i < ar.count; i++) {
            if (ar.planets[i].altitudeDeg >= 10.0f) {
                if (!top1) top1 = ar.planets[i].name;
                else if (!top2) top2 = ar.planets[i].name;
                vis_count++;
            }
        }
        if (top1 && top2) {
            snprintf(best, sizeof(best), "%s & %s", top1, top2);
            snprintf(sub,  sizeof(sub),  "%d planets visible above 10\xC2\xB0", vis_count);
        } else if (top1) {
            snprintf(best, sizeof(best), "%s", top1);
            snprintf(sub,  sizeof(sub),  "Best viewing - %.0f\xC2\xB0 altitude",
                     ar.planets[0].altitudeDeg);
        }
        lv_label_set_text(pl_best,     best);
        lv_label_set_text(pl_best_sub, sub);
    }

    // Updated timestamp
    {
        time_t now_t = time(nullptr);
        struct tm *lt = localtime(&now_t);
        snprintf(buf, sizeof(buf), "Fetched %02d:%02d", lt->tm_hour, lt->tm_min);
        lv_label_set_text(pl_updated, buf);
    }

    // Redraw compass
    render_planet_compass(ar);
}

// =============================================================================
//  BUILD: ALERTS SCREEN
// =============================================================================
static void build_alerts_screen() {
    lv_obj_t *scr = screens[SCREEN_ALERTS];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, LV_SYMBOL_WARNING "  Weather Warnings - Sydney");

    const int CY  = STATUSBAR_H + PAD;
    const int CX  = NAV_BTN_W + PAD;
    const int CW  = SCREEN_W - NAV_BTN_W * 2 - PAD * 2;
    const int CH  = SCREEN_H - STATUSBAR_H - PAD * 2 - 26;

    // Source badge — top-right, shows "BOM" or "WeatherAPI fallback"
    alerts_source_lbl = lv_label_create(scr);
    lv_label_set_text(alerts_source_lbl, "");
    lv_obj_set_style_text_font(alerts_source_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(alerts_source_lbl, C_ACCENT, 0);
    lv_obj_align(alerts_source_lbl, LV_ALIGN_TOP_RIGHT, -(NAV_BTN_W + PAD), PAD);

    // "No active warnings" label
    alerts_no_warn_lbl = lv_label_create(scr);
    lv_label_set_text(alerts_no_warn_lbl, LV_SYMBOL_OK "  No active weather warnings for this location");
    lv_obj_set_style_text_font(alerts_no_warn_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(alerts_no_warn_lbl, lv_color_hex(0x3FB950), 0);
    lv_obj_align(alerts_no_warn_lbl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_add_flag(alerts_no_warn_lbl, LV_OBJ_FLAG_HIDDEN);

    // Last-updated label
    alerts_updated_lbl = lv_label_create(scr);
    lv_label_set_text(alerts_updated_lbl, "");
    lv_obj_set_style_text_font(alerts_updated_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(alerts_updated_lbl, lv_color_hex(0x8B949E), 0);
    lv_obj_align(alerts_updated_lbl, LV_ALIGN_BOTTOM_MID, 0, -28);

    // Build up to ALERTS_MAX_CARDS alert cards
    // Cards stack vertically; each is 110px tall with a 4px left colour bar
    const int CARD_H   = (CH - PAD * (ALERTS_MAX_CARDS - 1)) / ALERTS_MAX_CARDS;
    const int BAR_W    = 6;
    const int CARD_PAD = 10;

    for (int i = 0; i < ALERTS_MAX_CARDS; i++) {
        int cy = CY + i * (CARD_H + PAD);

        // Outer card
        lv_obj_t *card = lv_obj_create(scr);
        lv_obj_set_size(card, CW, CARD_H);
        lv_obj_set_pos(card, CX, cy);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x161B22), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x30363D), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        alert_cards[i] = card;

        // Severity colour bar (left edge)
        lv_obj_t *bar = lv_obj_create(card);
        lv_obj_set_size(bar, BAR_W, CARD_H);
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x8B949E), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_style_radius(bar, 0, 0);
        lv_obj_set_style_pad_all(bar, 0, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        alert_sev_bar[i] = bar;

        int tx = BAR_W + CARD_PAD;
        int tw = CW - BAR_W - CARD_PAD * 2;

        // Headline (large, bold-ish)
        lv_obj_t *hl = lv_label_create(card);
        lv_obj_set_style_text_font(hl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(hl, lv_color_hex(0xE6EDF3), 0);
        lv_obj_set_style_bg_opa(hl, LV_OPA_TRANSP, 0);
        lv_obj_set_width(hl, tw);
        lv_label_set_long_mode(hl, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(hl, tx, CARD_PAD);
        lv_label_set_text(hl, "");
        alert_headline[i] = hl;

        // Meta: severity + areas
        lv_obj_t *meta = lv_label_create(card);
        lv_obj_set_style_text_font(meta, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(meta, lv_color_hex(0x8B949E), 0);
        lv_obj_set_style_bg_opa(meta, LV_OPA_TRANSP, 0);
        lv_obj_set_width(meta, tw);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(meta, tx, CARD_PAD + 26);
        lv_label_set_text(meta, "");
        alert_meta[i] = meta;

        // Time range
        lv_obj_t *tl = lv_label_create(card);
        lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tl, lv_color_hex(0x8B949E), 0);
        lv_obj_set_style_bg_opa(tl, LV_OPA_TRANSP, 0);
        lv_obj_set_width(tl, tw);
        lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(tl, tx, CARD_PAD + 44);
        lv_label_set_text(tl, "");
        alert_time[i] = tl;

        // Description (remaining height, wraps)
        lv_obj_t *desc = lv_label_create(card);
        lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(desc, lv_color_hex(0xE6EDF3), 0);
        lv_obj_set_style_bg_opa(desc, LV_OPA_TRANSP, 0);
        lv_obj_set_width(desc, tw);
        lv_obj_set_height(desc, CARD_H - CARD_PAD * 2 - 62);
        lv_label_set_long_mode(desc, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(desc, tx, CARD_PAD + 62);
        lv_label_set_text(desc, "");
        alert_desc[i] = desc;
    }

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  UPDATE: ALERTS SCREEN — BOM primary, WeatherAPI fallback
// =============================================================================
void ui_update_alerts(const BomWarningsResult &bom, const WeatherResult &wr) {
    if (!alert_cards[0]) return;

    for (int i = 0; i < ALERTS_MAX_CARDS; i++) {
        if (alert_cards[i]) lv_obj_add_flag(alert_cards[i], LV_OBJ_FLAG_HIDDEN);
    }

    bool use_bom = bom.valid;
    int  n       = 0;

    if (use_bom) {
        n = bom.count;
        if (n > ALERTS_MAX_CARDS) n = ALERTS_MAX_CARDS;
        if (alerts_source_lbl)
            lv_label_set_text(alerts_source_lbl, "Source: BOM");
        lv_obj_set_style_text_color(alerts_source_lbl, C_ACCENT, 0);
    } else {
        n = wr.alertCount;
        if (n > ALERTS_MAX_CARDS) n = ALERTS_MAX_CARDS;
        if (alerts_source_lbl)
            lv_label_set_text(alerts_source_lbl, "Source: WeatherAPI (BOM unavailable)");
        lv_obj_set_style_text_color(alerts_source_lbl, C_YELLOW, 0);
    }

    if (n == 0) {
        if (alerts_no_warn_lbl) lv_obj_clear_flag(alerts_no_warn_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (alerts_no_warn_lbl) lv_obj_add_flag(alerts_no_warn_lbl, LV_OBJ_FLAG_HIDDEN);

        for (int i = 0; i < n; i++) {
            lv_color_t col;
            const char *headline_str;
            char meta_buf[128]  = "";
            char time_buf[80]   = "";
            const char *desc_str;

            if (use_bom) {
                const BomWarning &bw = bom.warnings[i];
                col          = lv_color_hex(bom_warn_colour(bw.severity));
                headline_str = bw.title[0] ? bw.title : bw.type;

                if (bw.areas[0])
                    snprintf(meta_buf, sizeof(meta_buf), "%s  -  %s",
                             bw.severity, bw.areas);
                else
                    snprintf(meta_buf, sizeof(meta_buf), "%s", bw.severity);

                if (bw.expires[0]) {
                    char exp_short[20];
                    strncpy(exp_short, bw.expires, 16);
                    exp_short[16] = '\0';
                    for (int j = 0; j < 16; j++)
                        if (exp_short[j] == 'T') exp_short[j] = ' ';
                    snprintf(time_buf, sizeof(time_buf), "Until: %s UTC", exp_short);
                }

                desc_str = bw.desc[0] ? bw.desc : "See BOM website for details.";
            } else {
                const AlertInfo &al = wr.alerts[i];
                col          = alert_severity_colour(al.severity);
                headline_str = al.headline[0] ? al.headline : al.event;

                if (al.areas[0])
                    snprintf(meta_buf, sizeof(meta_buf), "%s  -  %s",
                             al.severity, al.areas);
                else
                    snprintf(meta_buf, sizeof(meta_buf), "%s", al.severity);

                if (al.onset[0] && al.expires[0])
                    snprintf(time_buf, sizeof(time_buf), "From: %s   Until: %s",
                             al.onset, al.expires);
                else if (al.expires[0])
                    snprintf(time_buf, sizeof(time_buf), "Until: %s", al.expires);

                desc_str = al.desc[0] ? al.desc : "No further details available.";
            }

            lv_obj_clear_flag(alert_cards[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(alert_sev_bar[i], col, 0);
            lv_obj_set_style_border_color(alert_cards[i], col, 0);

            lv_label_set_text(alert_headline[i], headline_str);
            lv_obj_set_style_text_color(alert_headline[i], col, 0);
            lv_label_set_text(alert_meta[i], meta_buf);
            lv_label_set_text(alert_time[i], time_buf);
            lv_label_set_text(alert_desc[i], desc_str);
        }
    }

    if (alerts_updated_lbl) {
        time_t now_t = time(nullptr);
        struct tm lt_buf;
        struct tm *lt = localtime(&now_t);
        if (lt) { lt_buf = *lt; lt = &lt_buf; }
        char upd[48];
        snprintf(upd, sizeof(upd), "Updated %02d:%02d",
                 lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
        lv_label_set_text(alerts_updated_lbl, upd);
    }
}

// =============================================================================
//  SETTINGS HELPER — save all settings to Preferences
// =============================================================================
static void settings_save() {
    Preferences prefs;
    prefs.begin("dash", false);
    prefs.putInt ("bright",    g_setting_brightness);
    prefs.putInt ("timeout",   g_setting_timeout_sec);
    prefs.putBool("dim",       g_setting_dim_before_sleep);
    prefs.putInt ("wx_int",    g_setting_wx_interval_sec);
    prefs.putInt ("sw_int",    g_setting_sw_interval_sec);
    prefs.putBool("wake_clk",  g_setting_wake_to_clock);
    prefs.end();
}

// =============================================================================
//  SETTINGS HELPER — highlight one button in a row, dim others
// =============================================================================
static void set_row_highlight(lv_obj_t **btns, int count, int active_idx) {
    for (int i = 0; i < count; i++) {
        if (!btns[i]) continue;
        bool active = (i == active_idx);
        lv_obj_set_style_bg_color(btns[i],
            active ? lv_color_hex(0x1C3A5E) : lv_color_hex(0x0D1117), 0);
        lv_obj_set_style_border_color(btns[i],
            active ? C_ACCENT : C_BORDER, 0);
        lv_obj_set_style_border_width(btns[i], active ? 2 : 1, 0);
        // Update label colour
        lv_obj_t *lbl = lv_obj_get_child(btns[i], 0);
        if (lbl) lv_obj_set_style_text_color(lbl,
            active ? C_ACCENT : C_DIM, 0);
    }
}

// =============================================================================
//  SETTINGS HELPER — create a row of option buttons
//  Returns array of button pointers via btns[].
// =============================================================================
static lv_obj_t* make_setting_row(lv_obj_t *parent,
                                   int y, int row_h,
                                   const char *label_text,
                                   const char **btn_labels, int btn_count,
                                   lv_obj_t **btns,
                                   lv_event_cb_t cb) {
    const int LEFT_W = 200;
    // 460 = LEFT_CARD_W(510) - PAD*2(48) - small margin — fits inside card
    const int COL_W  = 460;
    const int BTN_H  = 38;
    const int BTN_GAP = 6;
    int btn_total_w = COL_W - LEFT_W - PAD;
    int btn_w = (btn_total_w - BTN_GAP * (btn_count - 1)) / btn_count;

    // Row label
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, C_TEXT, 0);
    lv_obj_set_pos(lbl, 0, y + (row_h - 20) / 2);

    // Buttons
    for (int i = 0; i < btn_count; i++) {
        int bx = LEFT_W + i * (btn_w + BTN_GAP);
        int by = y + (row_h - BTN_H) / 2;
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_size(btn, btn_w, BTN_H);
        lv_obj_set_pos(btn, bx, by);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0D1117), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, C_BORDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t *bl = lv_label_create(btn);
        lv_label_set_text(bl, btn_labels[i]);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bl, C_DIM, 0);
        lv_obj_center(bl);
        if (btns) btns[i] = btn;
    }
    return nullptr;
}

// =============================================================================
//  SETTINGS CALLBACKS
// =============================================================================
static void set_bright_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_apply_brightness(BRIGHT_VALS[idx]);
    settings_save();
}
static void set_timeout_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_setting_timeout_sec = TIMEOUT_VALS[idx];
    set_row_highlight(set_timeout_btns, TIMEOUT_OPTS, idx);
    settings_save();
}
static void set_dim_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_setting_dim_before_sleep = (idx == 0);
    lv_obj_t *btns[2] = {set_dim_on, set_dim_off};
    set_row_highlight(btns, 2, idx);
    settings_save();
}
static void set_wx_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_setting_wx_interval_sec = WX_VALS[idx];
    set_row_highlight(set_wx_btns, WX_OPTS, idx);
    settings_save();
}
static void set_sw_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_setting_sw_interval_sec = SW_VALS[idx];
    set_row_highlight(set_sw_btns, SW_OPTS, idx);
    settings_save();
}
static void set_wake_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    g_setting_wake_to_clock = (idx == 1);
    lv_obj_t *btns[2] = {set_wake_last, set_wake_clock};
    set_row_highlight(btns, 2, idx);
    settings_save();
}
static void refresh_wx_cb(lv_event_t *e) { g_force_wx_refresh = true; }
static void refresh_sw_cb(lv_event_t *e) { g_force_sw_refresh = true; }


// =============================================================================
//  BUILD: SENSOR HISTORY SCREEN
// =============================================================================
// =============================================================================
//  HELPERS — draw one sensor stats chart onto ss_canvas
// =============================================================================
static void ss_draw_chart(const float *data, int count, lv_color_t col) {
    if (!ss_canvas || !ss_buf || count < 2) return;

    lv_obj_clean(ss_canvas);
    lv_canvas_set_buffer(ss_canvas, ss_buf, SS_CHART_W, SS_CHART_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(ss_canvas, C_CARD, LV_OPA_COVER);

    const int W = SS_CHART_W, H = SS_CHART_H;
    const int PAD_L = 56, PAD_R = 12, PAD_T = 10, PAD_B = 24;
    const int CW = W - PAD_L - PAD_R;
    const int CH = H - PAD_T - PAD_B;

    // ── Data range ────────────────────────────────────────────────────────────
    float dmin = data[0], dmax = data[0];
    for (int i = 1; i < count; i++) {
        if (data[i] < dmin) dmin = data[i];
        if (data[i] > dmax) dmax = data[i];
    }
    float range = dmax - dmin;
    if (range < 0.5f) range = 0.5f;
    float pad_v = range * 0.07f;
    float ylo = dmin - pad_v;
    float yhi = dmax + pad_v;

    // ── Axes ──────────────────────────────────────────────────────────────────
    canvas_line(ss_canvas, PAD_L, PAD_T, PAD_L, H - PAD_B, C_BORDER, 1);
    canvas_line(ss_canvas, PAD_L, H - PAD_B, W - PAD_R, H - PAD_B, C_BORDER, 1);

    // ── Y grid + labels (4 ticks) ────────────────────────────────────────────
    for (int ti = 0; ti < 4; ti++) {
        float frac = (float)ti / 3.0f;
        float val  = ylo + frac * (yhi - ylo);
        int   gy   = H - PAD_B - (int)(frac * CH);
        canvas_line(ss_canvas, PAD_L, gy, W - PAD_R, gy,
                    lv_color_hex(0x21262D), 1);
        canvas_line(ss_canvas, PAD_L - 4, gy, PAD_L, gy, C_DIM, 1);

        char ylbl[12];
        if (range < 2.0f)  snprintf(ylbl, sizeof(ylbl), "%.1f", val);
        else               snprintf(ylbl, sizeof(ylbl), "%.0f", val);

        lv_obj_t *yl = lv_label_create(ss_canvas);
        lv_label_set_text(yl, ylbl);
        lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(yl, C_DIM, 0);
        lv_obj_set_style_bg_opa(yl, LV_OPA_TRANSP, 0);
        lv_obj_set_size(yl, PAD_L - 6, 18);
        lv_obj_set_style_text_align(yl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(yl, 0, gy - 9);
    }

    // ── X axis time labels ────────────────────────────────────────────────────
    // For 8h: label every 24 slots (2h). For 24h: every ~24 slots. For 7d: every ~24 slots.
    int label_every = (count <= 96) ? 24 : (count / 5);
    if (label_every < 1) label_every = 1;
    for (int i = 0; i < count; i += label_every) {
        int bx = PAD_L + (int)((float)i / (count - 1) * CW);
        canvas_line(ss_canvas, bx, H - PAD_B, bx, H - PAD_B + 4, C_BORDER, 1);
        int mins_ago = (count - 1 - i) * ((ss_window == 2) ? 35 : 5);
        char xl[10];
        if (mins_ago == 0)         snprintf(xl, sizeof(xl), "now");
        else if (mins_ago < 60)    snprintf(xl, sizeof(xl), "%dm", mins_ago);
        else if (mins_ago < 1440)  snprintf(xl, sizeof(xl), "%dh", mins_ago / 60);
        else                       snprintf(xl, sizeof(xl), "%dd", mins_ago / 1440);
        lv_obj_t *xll = lv_label_create(ss_canvas);
        lv_label_set_text(xll, xl);
        lv_obj_set_style_text_font(xll, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(xll, C_DIM, 0);
        lv_obj_set_style_bg_opa(xll, LV_OPA_TRANSP, 0);
        lv_obj_set_size(xll, 40, 16);
        lv_obj_set_pos(xll, bx - 20, H - PAD_B + 5);
    }

    // ── Avg line ─────────────────────────────────────────────────────────────
    float sum = 0;
    for (int i = 0; i < count; i++) sum += data[i];
    float avg_val = sum / count;
    int avg_y = PAD_T + (int)((yhi - avg_val) / (yhi - ylo) * CH);
    if (avg_y >= PAD_T && avg_y <= H - PAD_B) {
        // dashed avg line — draw every other 4px segment
        for (int xi = PAD_L; xi < W - PAD_R; xi += 8)
            canvas_line(ss_canvas, xi, avg_y, xi + 4, avg_y,
                        lv_color_hex(0x58A6FF), 1);
        lv_obj_t *avg_lbl = lv_label_create(ss_canvas);
        lv_label_set_text(avg_lbl, "avg");
        lv_obj_set_style_text_font(avg_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(avg_lbl, lv_color_hex(0x58A6FF), 0);
        lv_obj_set_style_bg_opa(avg_lbl, LV_OPA_TRANSP, 0);
        lv_obj_set_pos(avg_lbl, W - PAD_R - 28, avg_y - 14);
    }

    // ── Area fill ─────────────────────────────────────────────────────────────
    lv_color_t fill_col = lv_color_mix(col, lv_color_hex(0x0D1117), 18);
    for (int xi = PAD_L; xi <= PAD_L + CW; xi++) {
        float frac = (float)(xi - PAD_L) / CW;
        float fi   = frac * (count - 1);
        int   i0   = (int)fi;
        if (i0 >= count - 1) i0 = count - 2;
        float t   = fi - i0;
        float val = data[i0] * (1.0f - t) + data[i0 + 1] * t;
        int   ly  = PAD_T + (int)((yhi - val) / (yhi - ylo) * CH);
        if (ly < PAD_T) ly = PAD_T;
        int base_y = H - PAD_B - 1;
        if (ly < base_y)
            canvas_line(ss_canvas, xi, ly, xi, base_y, fill_col, 1);
    }

    // ── Line (2px) ────────────────────────────────────────────────────────────
    for (int i = 0; i < count - 1; i++) {
        int x0 = PAD_L + (int)((float)i       / (count - 1) * CW);
        int x1 = PAD_L + (int)((float)(i + 1) / (count - 1) * CW);
        int y0 = PAD_T + (int)((yhi - data[i])     / (yhi - ylo) * CH);
        int y1 = PAD_T + (int)((yhi - data[i + 1]) / (yhi - ylo) * CH);
        canvas_line(ss_canvas, x0, y0,   x1, y1,   col, 1);
        canvas_line(ss_canvas, x0, y0-1, x1, y1-1, col, 1);
    }

    // ── Hi / Lo markers ───────────────────────────────────────────────────────
    int hi_idx = 0, lo_idx = 0;
    for (int i = 1; i < count; i++) {
        if (data[i] > data[hi_idx]) hi_idx = i;
        if (data[i] < data[lo_idx]) lo_idx = i;
    }

    int last_x = PAD_L + CW;   // "now" x position — used for clash detection below

    auto marker = [&](int idx, lv_color_t mcol, const char *pfx, float val, bool is_lo) {
        int mx = PAD_L + (int)((float)idx / (count - 1) * CW);
        int my = PAD_T + (int)((yhi - val) / (yhi - ylo) * CH);
        canvas_fill_circle(ss_canvas, mx, my, 4, mcol);
        canvas_line(ss_canvas, mx, my, mx, H - PAD_B, C_BORDER, 1);
        char lbl[16]; snprintf(lbl, sizeof(lbl), "%s%.1f", pfx, val);
        lv_obj_t *ml = lv_label_create(ss_canvas);
        lv_label_set_text(ml, lbl);
        lv_obj_set_style_text_font(ml, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ml, mcol, 0);
        lv_obj_set_style_bg_opa(ml, LV_OPA_TRANSP, 0);
        // If this is the Lo marker and it's within 60px of the "now" dot,
        // place the label below the dot instead of above to avoid the "now" label.
        int lbl_x = mx + 4;
        int lbl_y;
        if (is_lo && abs(mx - last_x) < 60) {
            lbl_y = my + 6;   // below the dot
        } else {
            lbl_y = my - 14;  // above the dot (default)
        }
        // Keep label within canvas bounds
        if (lbl_y < 0) lbl_y = 0;
        if (lbl_y > H - 16) lbl_y = H - 16;
        lv_obj_set_pos(ml, lbl_x, lbl_y);
    };
    marker(hi_idx, C_WARM, "Hi ", data[hi_idx], false);
    marker(lo_idx, C_COOL, "Lo ", data[lo_idx], true);

    // ── "now" dot at rightmost point ─────────────────────────────────────────
    int last_y = PAD_T + (int)((yhi - data[count - 1]) / (yhi - ylo) * CH);
    canvas_fill_circle(ss_canvas, last_x, last_y, 4, C_RED);
    lv_obj_t *now_lbl = lv_label_create(ss_canvas);
    lv_label_set_text(now_lbl, "now");
    lv_obj_set_style_text_font(now_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(now_lbl, C_RED, 0);
    lv_obj_set_style_bg_opa(now_lbl, LV_OPA_TRANSP, 0);
    // Place "now" label left of the dot so it doesn't clip the canvas edge
    lv_obj_set_pos(now_lbl, last_x - 32, last_y - 18);

    lv_obj_invalidate(ss_canvas);
}

// ── Helper: style a tab button as active or inactive ─────────────────────────
static void ss_style_tab(lv_obj_t *btn, bool active, int tab_idx) {
    // tab_idx: 0=temp(orange), 1=hum(blue), 2=pres(accent)
    lv_color_t act_cols[3] = { C_WARM, C_COOL, C_ACCENT };
    lv_color_t act_bg[3]   = { lv_color_hex(0x1A1008),
                                lv_color_hex(0x081828),
                                lv_color_hex(0x081828) };
    if (active) {
        lv_obj_set_style_bg_color(btn, act_bg[tab_idx], 0);
        lv_obj_set_style_border_color(btn, act_cols[tab_idx], 0);
        lv_obj_set_style_text_color(btn, act_cols[tab_idx], 0);
    } else {
        lv_obj_set_style_bg_color(btn, C_CARD, 0);
        lv_obj_set_style_border_color(btn, C_BORDER, 0);
        lv_obj_set_style_text_color(btn, C_DIM, 0);
    }
}

static void ss_style_win(lv_obj_t *btn, bool active) {
    if (active) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C3A5E), 0);
        lv_obj_set_style_border_color(btn, C_ACCENT, 0);
        lv_obj_set_style_text_color(btn, C_ACCENT, 0);
    } else {
        lv_obj_set_style_bg_color(btn, C_BG, 0);
        lv_obj_set_style_border_color(btn, C_BORDER, 0);
        lv_obj_set_style_text_color(btn, C_DIM, 0);
    }
}

// ── Tab/window button callbacks — stored as closures via user_data ────────────
struct SsTabData { int idx; };   // allocated once per button in build

static void ss_tab_cb(lv_event_t *e) {
    SsTabData *d = (SsTabData *)lv_event_get_user_data(e);
    if (!d) return;
    ss_tab = d->idx;
    for (int i = 0; i < 3; i++) ss_style_tab(ss_tab_btn[i], i == ss_tab, i);
    // Trigger chart redraw via invalidation flag — actual redraw happens in
    // next ui_update_sensor_stats() call (keeps this callback light)
    if (ss_canvas) lv_obj_invalidate(ss_canvas);
}

static void ss_win_cb(lv_event_t *e) {
    SsTabData *d = (SsTabData *)lv_event_get_user_data(e);
    if (!d) return;
    ss_window = d->idx;
    for (int i = 0; i < 3; i++) ss_style_win(ss_win_btn[i], i == ss_window);
    // Force extended reload on next update if switching to 24h or 7d
    if (ss_window != 0) ss_ext_window = -1;
    if (ss_canvas) lv_obj_invalidate(ss_canvas);
}

// =============================================================================
//  BUILD: SCREEN 11 — Sensor Stats
// =============================================================================
static void build_sensor_stats_screen() {
    lv_obj_t *scr = screens[SCREEN_SENSOR_HIST];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Indoor Sensor Statistics");

    // ── Layout constants ──────────────────────────────────────────────────────
    const int CY = STATUSBAR_H;
    const int CH = SCREEN_H - STATUSBAR_H - 26;   // 650 (leave 26px for dots)
    const int CX = NAV_BTN_W;                      // 70
    const int CW = SCREEN_W - NAV_BTN_W * 2;       // 1140
    const int SP = 12;   // screen padding (SP avoids clash with global PAD macro)

    // ── Tab row + window buttons ──────────────────────────────────────────────
    // Win buttons: 3 × 54px + 2 × 8px gap + 8px right margin = 186px reserved
    // Tabs share the remaining width with a small gap between them
    const int WIN_W    = 54;
    const int WIN_H    = 36;
    const int WIN_GAP  = 8;
    const int WIN_AREA = WIN_W * 3 + WIN_GAP * 2 + SP * 2;  // 186px
    const int TAB_H    = 44;
    const int TAB_Y    = CY + SP;
    const int TAB_TOTAL_W = CW - SP * 2 - WIN_AREA - SP;    // available for 3 tabs
    const int TAB_GAP  = 6;
    const int TAB_W    = (TAB_TOTAL_W - TAB_GAP * 2) / 3;
    const char *tab_labels[3]  = { "Temp", "Humidity", "Pressure" };
    const char *win_labels[3]  = { "8h", "24h", "7d" };

    // Persistent tab data structs (live for app lifetime — allocated once)
    static SsTabData tab_data[3] = { {0}, {1}, {2} };
    static SsTabData win_data[3] = { {0}, {1}, {2} };

    for (int i = 0; i < 3; i++) {
        int tx = CX + SP + i * (TAB_W + TAB_GAP);
        lv_obj_t *tb = lv_btn_create(scr);
        lv_obj_set_pos(tb, tx, TAB_Y);
        lv_obj_set_size(tb, TAB_W, TAB_H);
        lv_obj_set_style_radius(tb, 8, 0);
        lv_obj_set_style_border_width(tb, 1, 0);
        lv_obj_set_style_pad_all(tb, 0, 0);
        lv_obj_set_style_shadow_width(tb, 0, 0);
        lv_obj_t *lbl = lv_label_create(tb);
        lv_label_set_text(lbl, tab_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(tb, ss_tab_cb, LV_EVENT_CLICKED, &tab_data[i]);
        ss_tab_btn[i] = tb;
        ss_style_tab(tb, i == 0, i);
    }

    // Window buttons — right-aligned on same row
    int win_total_x = CX + CW - SP - WIN_W * 3 - WIN_GAP * 2;
    int win_y = TAB_Y + (TAB_H - WIN_H) / 2;
    for (int i = 0; i < 3; i++) {
        int wx = win_total_x + i * (WIN_W + WIN_GAP);
        lv_obj_t *wb = lv_btn_create(scr);
        lv_obj_set_pos(wb, wx, win_y);
        lv_obj_set_size(wb, WIN_W, WIN_H);
        lv_obj_set_style_radius(wb, 6, 0);
        lv_obj_set_style_border_width(wb, 1, 0);
        lv_obj_set_style_pad_all(wb, 0, 0);
        lv_obj_set_style_shadow_width(wb, 0, 0);
        lv_obj_t *wl = lv_label_create(wb);
        lv_label_set_text(wl, win_labels[i]);
        lv_obj_set_style_text_font(wl, &lv_font_montserrat_14, 0);
        lv_obj_center(wl);
        lv_obj_add_event_cb(wb, ss_win_cb, LV_EVENT_CLICKED, &win_data[i]);
        ss_win_btn[i] = wb;
        ss_style_win(wb, i == 0);
    }

    // ── Main content area — chart card + stats column ─────────────────────────
    const int BODY_Y  = TAB_Y + TAB_H + SP;
    const int BODY_H  = CH - (BODY_Y - CY) - SP;
    const int STATS_W = 248;
    const int CHART_CARD_W = CW - SP * 2 - SP - STATS_W;

    // Chart card
    lv_obj_t *chart_card = make_card_nopad(scr,
        CX + SP, BODY_Y, CHART_CARD_W, BODY_H);
    lv_obj_set_style_pad_all(chart_card, SP, 0);

    // Chart header row: current value (left) + Hi/Lo/Avg (right)
    ss_cur_val = lv_label_create(chart_card);
    lv_label_set_text(ss_cur_val, "--");
    lv_obj_set_style_text_font(ss_cur_val, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ss_cur_val, C_WARM, 0);
    lv_obj_align(ss_cur_val, LV_ALIGN_TOP_LEFT, 0, 0);

    ss_hi_lo = lv_label_create(chart_card);
    lv_label_set_text(ss_hi_lo, "");
    lv_obj_set_style_text_font(ss_hi_lo, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ss_hi_lo, C_DIM, 0);
    lv_obj_align(ss_hi_lo, LV_ALIGN_TOP_RIGHT, 0, 2);

    ss_avg_rng = lv_label_create(chart_card);
    lv_label_set_text(ss_avg_rng, "");
    lv_obj_set_style_text_font(ss_avg_rng, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ss_avg_rng, C_DIM, 0);
    lv_obj_align(ss_avg_rng, LV_ALIGN_TOP_RIGHT, 0, 20);

    // Chart canvas — fills remaining height below the header
    const int HDR_H   = 42;
    const int CANVAS_W = CHART_CARD_W - SP * 2;   // unused but kept for clarity
    const int CANVAS_H = BODY_H - SP * 2 - HDR_H; // unused but kept for clarity
    (void)CANVAS_W; (void)CANVAS_H;

    // Allocate PSRAM buffer for chart
    size_t buf_sz = (size_t)SS_CHART_W * SS_CHART_H * sizeof(lv_color_t);
    ss_buf = (lv_color_t *)heap_caps_malloc(buf_sz,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ss_buf) ss_buf = (lv_color_t *)malloc(buf_sz);

    ss_canvas = lv_canvas_create(chart_card);
    lv_canvas_set_buffer(ss_canvas, ss_buf, SS_CHART_W, SS_CHART_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(ss_canvas, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_canvas_fill_bg(ss_canvas, C_CARD, LV_OPA_COVER);

    // ── Stats column ──────────────────────────────────────────────────────────
    const int SC_X = CX + SP + CHART_CARD_W + SP;
    const int SC_W = STATS_W;
    const int CARD_GAP = 8;
    const int SC_CARD_H = (BODY_H - CARD_GAP * 3) / 4;

    // Helper: make a stats card and populate label pointers
    auto make_stat_card = [&](int card_y, const char *title,
                               lv_obj_t **val_lbl, lv_obj_t **hi_lbl,
                               lv_obj_t **lo_lbl, lv_obj_t **avg_lbl) {
        lv_obj_t *card = make_card_nopad(scr, SC_X, BODY_Y + card_y, SC_W, SC_CARD_H);
        lv_obj_set_style_pad_all(card, 10, 0);

        make_label(card, title, &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

        if (val_lbl) {
            *val_lbl = lv_label_create(card);
            lv_label_set_text(*val_lbl, "--");
            lv_obj_set_style_text_font(*val_lbl, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(*val_lbl, C_TEXT, 0);
            lv_obj_align(*val_lbl, LV_ALIGN_TOP_LEFT, 0, 20);
        }

        if (hi_lbl) {
            *hi_lbl = lv_label_create(card);
            lv_label_set_text(*hi_lbl, "Hi --");
            lv_obj_set_style_text_font(*hi_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(*hi_lbl, C_WARM, 0);
            lv_obj_align(*hi_lbl, LV_ALIGN_TOP_LEFT, 0, 46);
        }

        if (lo_lbl) {
            *lo_lbl = lv_label_create(card);
            lv_label_set_text(*lo_lbl, "Lo --");
            lv_obj_set_style_text_font(*lo_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(*lo_lbl, C_COOL, 0);
            lv_obj_align(*lo_lbl, LV_ALIGN_TOP_RIGHT, 0, 46);
        }

        if (avg_lbl) {
            *avg_lbl = lv_label_create(card);
            lv_label_set_text(*avg_lbl, "Avg --");
            lv_obj_set_style_text_font(*avg_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(*avg_lbl, C_DIM, 0);
            lv_obj_align(*avg_lbl, LV_ALIGN_TOP_LEFT, 0, 66);
        }
    };

    // TODAY card
    make_stat_card(0, "TODAY",
                   &ss_today_val, &ss_today_hi, &ss_today_lo, &ss_today_avg);

    // YESTERDAY card
    make_stat_card(SC_CARD_H + CARD_GAP, "YESTERDAY",
                   &ss_yest_val, &ss_yest_hi, &ss_yest_lo, &ss_yest_avg);

    // 7-DAY card (no big val — just hi/lo/avg + trend vs last week)
    {
        lv_obj_t *card = make_card_nopad(scr,
            SC_X, BODY_Y + (SC_CARD_H + CARD_GAP) * 2, SC_W, SC_CARD_H);
        lv_obj_set_style_pad_all(card, 10, 0);
        make_label(card, "7-DAY", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

        ss_7d_hi = lv_label_create(card);
        lv_label_set_text(ss_7d_hi, "Hi --");
        lv_obj_set_style_text_font(ss_7d_hi, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ss_7d_hi, C_WARM, 0);
        lv_obj_align(ss_7d_hi, LV_ALIGN_TOP_LEFT, 0, 20);

        ss_7d_lo = lv_label_create(card);
        lv_label_set_text(ss_7d_lo, "Lo --");
        lv_obj_set_style_text_font(ss_7d_lo, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ss_7d_lo, C_COOL, 0);
        lv_obj_align(ss_7d_lo, LV_ALIGN_TOP_RIGHT, 0, 20);

        ss_7d_avg = lv_label_create(card);
        lv_label_set_text(ss_7d_avg, "Avg --");
        lv_obj_set_style_text_font(ss_7d_avg, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ss_7d_avg, C_DIM, 0);
        lv_obj_align(ss_7d_avg, LV_ALIGN_TOP_LEFT, 0, 40);

        ss_7d_trend = lv_label_create(card);
        lv_label_set_text(ss_7d_trend, "");
        lv_obj_set_style_text_font(ss_7d_trend, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ss_7d_trend, C_DIM, 0);
        lv_obj_align(ss_7d_trend, LV_ALIGN_TOP_LEFT, 0, 60);
    }

    // TREND card
    {
        lv_obj_t *card = make_card_nopad(scr,
            SC_X, BODY_Y + (SC_CARD_H + CARD_GAP) * 3, SC_W, SC_CARD_H);
        lv_obj_set_style_pad_all(card, 10, 0);
        make_label(card, "TREND", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

        ss_trend_dir = lv_label_create(card);
        lv_label_set_text(ss_trend_dir, "...");
        lv_obj_set_style_text_font(ss_trend_dir, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(ss_trend_dir, C_DIM, 0);
        lv_obj_align(ss_trend_dir, LV_ALIGN_TOP_LEFT, 0, 20);

        ss_trend_rate = lv_label_create(card);
        lv_label_set_text(ss_trend_rate, "");
        lv_obj_set_style_text_font(ss_trend_rate, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ss_trend_rate, C_DIM, 0);
        lv_obj_align(ss_trend_rate, LV_ALIGN_TOP_LEFT, 0, 46);
    }

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  UPDATE: SENSOR STATS — called from loop() on every sensor read
// =============================================================================
void ui_update_sensor_stats(const SensorData &sd, const char* date_today) {
    if (!ss_canvas) return;

    // ── Per-tab config ────────────────────────────────────────────────────────
    const char    *units[3]    = { "\xC2\xB0""C", "%", " hPa" };
    lv_color_t     cols[3]     = { C_WARM, C_COOL, C_ACCENT };
    const float   *hist8h[3]   = { sd.tempHistory, sd.humHistory, sd.pressHistory };
    float          cur_vals[3] = { sd.tempC, sd.humidity, sd.pressureHPa };

    lv_color_t col  = cols[ss_tab];
    const char *unit = units[ss_tab];

    // ── Update current value header ───────────────────────────────────────────
    {
        char buf[24];
        if (ss_tab == 0)      snprintf(buf, sizeof(buf), "%.1f%s", cur_vals[ss_tab], unit);
        else if (ss_tab == 1) snprintf(buf, sizeof(buf), "%.0f%s", cur_vals[ss_tab], unit);
        else                  snprintf(buf, sizeof(buf), "%.1f%s", cur_vals[ss_tab], unit);
        lv_label_set_text(ss_cur_val, buf);
        lv_obj_set_style_text_color(ss_cur_val, col, 0);
    }

    // ── Determine which data array + count to chart ───────────────────────────
    const float *chart_data  = nullptr;
    int          chart_count = 0;

    if (ss_window == 0) {
        // 8h — use live history from SensorData (already in PSRAM)
        chart_data  = hist8h[ss_tab];
        chart_count = sd.histSlotCount;

    } else {
        // 24h or 7d — load from SD if needed.
        // 24h uses 2 days (today + yesterday) so the Yesterday stats card populates.
        // 7d uses 7 days. Stats are computed per-day inside sd_read_extended().
        int need_days = (ss_window == 1) ? 2 : 7;
        if (ss_ext_window != ss_window) {
            // (Re)load extended history
            if (!ss_ext_temp) {
                ss_ext_temp = (float*)heap_caps_malloc(SD_EXT_SLOTS_24H * sizeof(float),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                ss_ext_hum  = (float*)heap_caps_malloc(SD_EXT_SLOTS_24H * sizeof(float),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                ss_ext_pres = (float*)heap_caps_malloc(SD_EXT_SLOTS_24H * sizeof(float),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!ss_ext_temp) {
                    // Fallback to internal RAM
                    ss_ext_temp = (float*)malloc(SD_EXT_SLOTS_24H * sizeof(float));
                    ss_ext_hum  = (float*)malloc(SD_EXT_SLOTS_24H * sizeof(float));
                    ss_ext_pres = (float*)malloc(SD_EXT_SLOTS_24H * sizeof(float));
                }
            }
            if (ss_ext_temp && ss_ext_hum && ss_ext_pres && sd_available()) {
                ss_ext_stats  = sd_read_extended(ss_ext_temp, ss_ext_hum, ss_ext_pres,
                                                 SD_EXT_SLOTS_24H, need_days, date_today);
                ss_ext_count  = ss_ext_stats.slots_loaded;
                ss_ext_window = ss_window;
            }
        }
        if (ss_ext_count > 1 && ss_ext_temp) {
            const float *ext_arrays[3] = { ss_ext_temp, ss_ext_hum, ss_ext_pres };
            chart_data  = ext_arrays[ss_tab];
            chart_count = ss_ext_count;
        } else {
            // No SD data yet — fall back to 8h
            chart_data  = hist8h[ss_tab];
            chart_count = sd.histSlotCount;
        }
    }

    // ── Chart header Hi/Lo/Avg ─────────────────────────────────────────────────
    if (chart_count >= 2) {
        float mn = chart_data[0], mx = chart_data[0], sm = 0;
        for (int i = 0; i < chart_count; i++) {
            if (chart_data[i] < mn) mn = chart_data[i];
            if (chart_data[i] > mx) mx = chart_data[i];
            sm += chart_data[i];
        }
        float av = sm / chart_count;
        char buf[48];
        if (ss_tab != 1)
            snprintf(buf, sizeof(buf), "Hi %.1f%s   Lo %.1f%s",
                     mx, unit, mn, unit);
        else
            snprintf(buf, sizeof(buf), "Hi %.0f%s   Lo %.0f%s",
                     mx, unit, mn, unit);
        lv_label_set_text(ss_hi_lo, buf);

        if (ss_tab != 1)
            snprintf(buf, sizeof(buf), "Avg %.1f%s   Range %.1f%s",
                     av, unit, mx - mn, unit);
        else
            snprintf(buf, sizeof(buf), "Avg %.0f%s   Range %.0f%s",
                     av, unit, mx - mn, unit);
        lv_label_set_text(ss_avg_rng, buf);
    } else {
        lv_label_set_text(ss_hi_lo,  "Collecting data...");
        lv_label_set_text(ss_avg_rng, "");
    }

    // ── Draw chart ────────────────────────────────────────────────────────────
    if (chart_count >= 2) {
        ss_draw_chart(chart_data, chart_count, col);
    }

    // ── Stats column — TODAY card ─────────────────────────────────────────────
    // Pull values: for 8h window use history; for 24h/7d use ss_ext_stats
    auto fmt_val = [&](char *out, size_t sz, float v) {
        if (ss_tab != 1) snprintf(out, sz, "%.1f%s", v, unit);
        else             snprintf(out, sz, "%.0f%s", v, unit);
    };

    char vbuf[20];
    // TODAY
    {
        float today_hi, today_lo, today_avg;
        if (ss_window == 0 && sd.histSlotCount >= 2) {
            // Derive from 8h history
            float mn=hist8h[ss_tab][0], mx=hist8h[ss_tab][0], sm=0;
            for (int i=0; i<sd.histSlotCount; i++) {
                if (hist8h[ss_tab][i]<mn) mn=hist8h[ss_tab][i];
                if (hist8h[ss_tab][i]>mx) mx=hist8h[ss_tab][i];
                sm+=hist8h[ss_tab][i];
            }
            today_hi=mx; today_lo=mn; today_avg=sm/sd.histSlotCount;
        } else if (ss_ext_window >= 0) {
            float *hi_arr[3]  = {&ss_ext_stats.temp_today_hi, &ss_ext_stats.hum_today_hi, &ss_ext_stats.pres_today_hi};
            float *lo_arr[3]  = {&ss_ext_stats.temp_today_lo, &ss_ext_stats.hum_today_lo, &ss_ext_stats.pres_today_lo};
            float *avg_arr[3] = {&ss_ext_stats.temp_today_avg,&ss_ext_stats.hum_today_avg,&ss_ext_stats.pres_today_avg};
            today_hi=*hi_arr[ss_tab]; today_lo=*lo_arr[ss_tab]; today_avg=*avg_arr[ss_tab];
        } else {
            today_hi=today_lo=today_avg=0;
        }

        fmt_val(vbuf, sizeof(vbuf), cur_vals[ss_tab]);
        lv_label_set_text(ss_today_val, vbuf);
        lv_obj_set_style_text_color(ss_today_val, col, 0);

        fmt_val(vbuf, sizeof(vbuf), today_hi);
        char hbuf[24]; snprintf(hbuf, sizeof(hbuf), "Hi %s", vbuf);
        lv_label_set_text(ss_today_hi, hbuf);

        fmt_val(vbuf, sizeof(vbuf), today_lo);
        char lbuf[24]; snprintf(lbuf, sizeof(lbuf), "Lo %s", vbuf);
        lv_label_set_text(ss_today_lo, lbuf);

        fmt_val(vbuf, sizeof(vbuf), today_avg);
        char abuf[24]; snprintf(abuf, sizeof(abuf), "Avg %s", vbuf);
        lv_label_set_text(ss_today_avg, abuf);
    }

    // YESTERDAY
    if (ss_ext_window >= 0) {
        float *hi_arr[3]  = {&ss_ext_stats.temp_yest_hi, &ss_ext_stats.hum_yest_hi, &ss_ext_stats.pres_yest_hi};
        float *lo_arr[3]  = {&ss_ext_stats.temp_yest_lo, &ss_ext_stats.hum_yest_lo, &ss_ext_stats.pres_yest_lo};
        float *avg_arr[3] = {&ss_ext_stats.temp_yest_avg,&ss_ext_stats.hum_yest_avg,&ss_ext_stats.pres_yest_avg};

        float yh=*hi_arr[ss_tab], yl=*lo_arr[ss_tab], ya=*avg_arr[ss_tab];

        fmt_val(vbuf, sizeof(vbuf), yh);
        lv_label_set_text(ss_yest_val, vbuf);
        lv_obj_set_style_text_color(ss_yest_val, C_DIM, 0);

        char hbuf[24], lbuf[24], abuf[24];
        fmt_val(vbuf, sizeof(vbuf), yh); snprintf(hbuf, sizeof(hbuf), "Hi %s", vbuf);
        lv_label_set_text(ss_yest_hi, hbuf);
        fmt_val(vbuf, sizeof(vbuf), yl); snprintf(lbuf, sizeof(lbuf), "Lo %s", vbuf);
        lv_label_set_text(ss_yest_lo, lbuf);
        fmt_val(vbuf, sizeof(vbuf), ya); snprintf(abuf, sizeof(abuf), "Avg %s", vbuf);
        lv_label_set_text(ss_yest_avg, abuf);
    } else {
        lv_label_set_text(ss_yest_val, "--");
        lv_label_set_text(ss_yest_hi, "Hi --");
        lv_label_set_text(ss_yest_lo, "Lo --");
        lv_label_set_text(ss_yest_avg, "Avg --");
    }

    // 7-DAY
    if (ss_ext_window == 1 || ss_ext_window == 2) {  // only populated in 24h or 7d load
        float *hi_arr[3]  = {&ss_ext_stats.temp_7d_hi, &ss_ext_stats.hum_7d_hi, &ss_ext_stats.pres_7d_hi};
        float *lo_arr[3]  = {&ss_ext_stats.temp_7d_lo, &ss_ext_stats.hum_7d_lo, &ss_ext_stats.pres_7d_lo};
        float *avg_arr[3] = {&ss_ext_stats.temp_7d_avg,&ss_ext_stats.hum_7d_avg,&ss_ext_stats.pres_7d_avg};
        float *vs_arr[3]  = {&ss_ext_stats.temp_7d_vs_prev,&ss_ext_stats.hum_7d_vs_prev,&ss_ext_stats.pres_7d_vs_prev};

        char hbuf[24], lbuf[24], abuf[24], tbuf[32];
        fmt_val(vbuf, sizeof(vbuf), *hi_arr[ss_tab]); snprintf(hbuf, sizeof(hbuf), "Hi %s", vbuf);
        lv_label_set_text(ss_7d_hi, hbuf);
        fmt_val(vbuf, sizeof(vbuf), *lo_arr[ss_tab]); snprintf(lbuf, sizeof(lbuf), "Lo %s", vbuf);
        lv_label_set_text(ss_7d_lo, lbuf);
        fmt_val(vbuf, sizeof(vbuf), *avg_arr[ss_tab]); snprintf(abuf, sizeof(abuf), "Avg %s", vbuf);
        lv_label_set_text(ss_7d_avg, abuf);

        float vs = *vs_arr[ss_tab];
        if (!isnan(vs)) {
            if (ss_tab != 1) snprintf(tbuf, sizeof(tbuf), "%+.1f vs prev wk", vs);
            else             snprintf(tbuf, sizeof(tbuf), "%+.0f%% vs prev wk", vs);
            lv_label_set_text(ss_7d_trend, tbuf);
            lv_obj_set_style_text_color(ss_7d_trend,
                vs > 0 ? C_WARM : (vs < 0 ? C_COOL : C_DIM), 0);
        } else {
            lv_label_set_text(ss_7d_trend, "Needs 14 days data");
            lv_obj_set_style_text_color(ss_7d_trend, C_DIM, 0);
        }
    } else {
        lv_label_set_text(ss_7d_hi, "Hi --");
        lv_label_set_text(ss_7d_lo, "Lo --");
        lv_label_set_text(ss_7d_avg, "Avg --");
        lv_label_set_text(ss_7d_trend, ss_window == 0 ? "Switch to 7d" : "Loading...");
        lv_obj_set_style_text_color(ss_7d_trend, C_DIM, 0);
    }

    // TREND card — use pressure trend for pressure tab, derive from history for others
    {
        const char *dir_str  = "Steady";
        lv_color_t  dir_col  = C_DIM;
        char        rate_buf[24] = "";

        if (ss_tab == 2) {
            // Pressure — use existing trend data
            switch (sd.pressureTrend) {
                case SensorData::TREND_RISING:
                    dir_str = "Rising";  dir_col = C_GREEN; break;
                case SensorData::TREND_FALLING:
                    dir_str = "Falling"; dir_col = C_WARM;  break;
                default:
                    dir_str = "Steady";  dir_col = C_DIM;   break;
            }
            if (sd.trendRateHPa_hr > 0.05f || sd.trendRateHPa_hr < -0.05f)
                snprintf(rate_buf, sizeof(rate_buf), "%+.1f hPa/hr", sd.trendRateHPa_hr);
        } else if (sd.histSlotCount >= 12) {
            // Derive simple trend from last 12 slots of the relevant history
            const float *h = hist8h[ss_tab];
            int n = sd.histSlotCount;
            float recent = 0, older = 0;
            int r_count = 0, o_count = 0;
            for (int i = n - 6; i < n; i++) if (i >= 0) { recent += h[i]; r_count++; }
            for (int i = n - 12; i < n - 6; i++) if (i >= 0) { older += h[i]; o_count++; }
            if (r_count > 0 && o_count > 0) {
                float diff = (recent / r_count) - (older / o_count);
                float thr = (ss_tab == 0) ? 0.2f : 1.0f;
                if (diff > thr)       { dir_str = "Rising";  dir_col = C_GREEN; }
                else if (diff < -thr) { dir_str = "Falling"; dir_col = C_WARM; }
                if (ss_tab == 0)
                    snprintf(rate_buf, sizeof(rate_buf), "%+.2f/hr", diff * 12);
                else
                    snprintf(rate_buf, sizeof(rate_buf), "%+.1f%%/hr", diff * 12);
            }
        }

        lv_label_set_text(ss_trend_dir, dir_str);
        lv_obj_set_style_text_color(ss_trend_dir, dir_col, 0);
        lv_label_set_text(ss_trend_rate, rate_buf);
    }
}

// =============================================================================
//  SCREEN 12: TIDES — Fort Denison, Sydney Harbour
// =============================================================================

// Module-level constant so both build and update functions share it
#define TI_LEFT_CARD_W  340
#define TI_CANVAS_W     512
#define TI_CANVAS_H     330

static lv_obj_t  *ti_next_type    = nullptr;
static lv_obj_t  *ti_next_height  = nullptr;
static lv_obj_t  *ti_next_time    = nullptr;
static lv_obj_t  *ti_next_in      = nullptr;
static lv_obj_t  *ti_after_type   = nullptr;
static lv_obj_t  *ti_after_height = nullptr;
static lv_obj_t  *ti_after_time   = nullptr;
static lv_obj_t  *ti_after_in     = nullptr;
static lv_obj_t  *ti_cur_height   = nullptr;
static lv_obj_t  *ti_range        = nullptr;
static lv_obj_t  *ti_dir_bar_fill = nullptr;
static lv_obj_t  *ti_dir_label    = nullptr;
static lv_obj_t  *ti_today_list   = nullptr;
static lv_obj_t  *ti_tmrw_list    = nullptr;
static lv_obj_t  *ti_nosd_lbl     = nullptr;
static lv_obj_t  *ti_canvas       = nullptr;
static lv_color_t *ti_canvas_buf  = nullptr;

struct TideEvent {
    int   year, month, day;
    int   hhmm;
    bool  isHigh;
    float height;
};

#define TIDE_MAX_EVENTS 16

static bool parse_tide_line(const char *line, TideEvent &ev) {
    if (!line || line[0] == '#' || line[0] == '\0') return false;
    int yr, mo, dy, hh, mm;
    char hl;
    float ht;
    if (sscanf(line, "%4d-%2d-%2d,%2d%2d,%c,%f",
               &yr, &mo, &dy, &hh, &mm, &hl, &ht) != 7) return false;
    ev.year = yr; ev.month = mo; ev.day = dy;
    ev.hhmm = hh * 100 + mm;
    ev.isHigh = (hl == 'H' || hl == 'h');
    ev.height = ht;
    return true;
}

static int load_tide_events(int today_yr, int today_mo, int today_dy,
                             int tmrw_yr,  int tmrw_mo,  int tmrw_dy,
                             TideEvent *out, int max_out) {
    if (!sd_available()) return 0;
    File f = SD.open("/tides.csv", FILE_READ);
    if (!f) { Serial.println("[Tides] /tides.csv not found"); return 0; }
    int count = 0;
    char line[64];
    while (f.available() && count < max_out) {
        int i = 0;
        while (f.available() && i < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c != '\r') line[i++] = c;
        }
        line[i] = '\0';
        TideEvent ev;
        if (!parse_tide_line(line, ev)) continue;
        bool is_today = (ev.year == today_yr && ev.month == today_mo && ev.day == today_dy);
        bool is_tmrw  = (ev.year == tmrw_yr  && ev.month == tmrw_mo  && ev.day == tmrw_dy);
        if (is_today || is_tmrw) out[count++] = ev;
        if (ev.year > tmrw_yr ||
            (ev.year == tmrw_yr && ev.month > tmrw_mo) ||
            (ev.year == tmrw_yr && ev.month == tmrw_mo && ev.day > tmrw_dy)) break;
    }
    f.close();
    return count;
}

static float interpolate_tide(const TideEvent *events, int n_events,
                               int today_yr, int today_mo, int today_dy,
                               int minute_of_day) {
    if (n_events < 2) return 0.5f;
    int prev_idx = -1;
    for (int i = 0; i < n_events; i++) {
        if (events[i].year != today_yr || events[i].month != today_mo ||
            events[i].day  != today_dy) continue;
        int ev_min = (events[i].hhmm / 100) * 60 + (events[i].hhmm % 100);
        if (ev_min <= minute_of_day) prev_idx = i;
    }
    if (prev_idx < 0) {
        for (int i = 0; i < n_events; i++)
            if (events[i].year == today_yr && events[i].month == today_mo &&
                events[i].day  == today_dy)
                return events[i].height;
        return 0.5f;
    }
    int next_idx = prev_idx + 1;
    if (next_idx >= n_events) return events[prev_idx].height;
    int t0 = (events[prev_idx].hhmm / 100) * 60 + (events[prev_idx].hhmm % 100);
    int t1 = (events[next_idx].hhmm / 100) * 60 + (events[next_idx].hhmm % 100);
    if (events[next_idx].year != today_yr || events[next_idx].month != today_mo ||
        events[next_idx].day  != today_dy) t1 += 1440;
    float h0 = events[prev_idx].height;
    float h1 = events[next_idx].height;
    float frac = (t1 == t0) ? 0.0f : (float)(minute_of_day - t0) / (float)(t1 - t0);
    float cos_frac = (1.0f - cosf(frac * (float)M_PI)) / 2.0f;
    return h0 + (h1 - h0) * cos_frac;
}

static void draw_tide_canvas(const TideEvent *events, int n_events,
                              int today_yr, int today_mo, int today_dy,
                              int cur_min) {
    if (!ti_canvas || !ti_canvas_buf) return;
    const int W = TI_CANVAS_W, H = TI_CANVAS_H;
    const int AXIS_L = 38, AXIS_B = 26;
    const int PW = W - AXIS_L - 6, PH = H - AXIS_B - 14;
    const int PX = AXIS_L, PY = 12;

    lv_canvas_fill_bg(ti_canvas, lv_color_hex(0x0D1117), LV_OPA_COVER);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    lv_draw_label_dsc_t lbl_dsc;
    lv_draw_label_dsc_init(&lbl_dsc);
    lbl_dsc.font  = &lv_font_montserrat_14;
    lbl_dsc.color = lv_color_hex(0x8B949E);

    float h_min = 99.0f, h_max = -99.0f;
    for (int i = 0; i < n_events; i++) {
        if (events[i].year != today_yr || events[i].month != today_mo ||
            events[i].day  != today_dy) continue;
        if (events[i].height < h_min) h_min = events[i].height;
        if (events[i].height > h_max) h_max = events[i].height;
    }
    if (h_max <= h_min) { h_min = 0.0f; h_max = 2.0f; }
    float h_pad = (h_max - h_min) * 0.15f;
    float y_lo = h_min - h_pad; if (y_lo < 0.0f) y_lo = 0.0f;
    float y_hi = h_max + h_pad;

    auto htp = [&](float h) -> int {
        float frac = (h - y_lo) / (y_hi - y_lo);
        return PY + PH - (int)(frac * PH);
    };
    auto mtp = [&](int m) -> int {
        return PX + (int)((float)m / 1440.0f * PW);
    };

    line_dsc.width = 1;
    for (float gh = 0.0f; gh <= y_hi + 0.01f; gh += 0.5f) {
        if (gh < y_lo) continue;
        int gy = htp(gh);
        line_dsc.color = lv_color_hex(0x21262D);
        lv_point_t pts[2] = {{(lv_coord_t)PX,(lv_coord_t)gy},{(lv_coord_t)(PX+PW),(lv_coord_t)gy}};
        lv_canvas_draw_line(ti_canvas, pts, 2, &line_dsc);
        char lb[8]; snprintf(lb, sizeof(lb), "%.1f", gh);
        lv_canvas_draw_text(ti_canvas, 2, gy - 8, 34, &lbl_dsc, lb);
    }
    for (int hr = 0; hr <= 24; hr += 6) {
        int gx = mtp(hr * 60);
        char lb[6]; snprintf(lb, sizeof(lb), "%02d:00", hr % 24);
        lv_canvas_draw_text(ti_canvas, gx - 16, H - AXIS_B + 4, 44, &lbl_dsc, lb);
        line_dsc.color = lv_color_hex(0x21262D);
        lv_point_t pts[2] = {{(lv_coord_t)gx,(lv_coord_t)PY},{(lv_coord_t)gx,(lv_coord_t)(PY+PH)}};
        lv_canvas_draw_line(ti_canvas, pts, 2, &line_dsc);
    }

    int prev_cx = -1, prev_cy = -1;
    for (int px = 0; px <= PW; px++) {
        int minute = (int)((float)px / (float)PW * 1440.0f);
        float h = interpolate_tide(events, n_events, today_yr, today_mo, today_dy, minute);
        int cx = PX + px, cy = htp(h);
        line_dsc.color = lv_color_hex(0x0D2A4A); line_dsc.width = 1;
        lv_point_t fill[2] = {{(lv_coord_t)cx,(lv_coord_t)cy},{(lv_coord_t)cx,(lv_coord_t)(PY+PH)}};
        lv_canvas_draw_line(ti_canvas, fill, 2, &line_dsc);
        if (prev_cx >= 0) {
            line_dsc.color = lv_color_hex(0x58A6FF); line_dsc.width = 2;
            lv_point_t seg[2] = {{(lv_coord_t)prev_cx,(lv_coord_t)prev_cy},{(lv_coord_t)cx,(lv_coord_t)cy}};
            lv_canvas_draw_line(ti_canvas, seg, 2, &line_dsc);
        }
        prev_cx = cx; prev_cy = cy;
    }

    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    for (int i = 0; i < n_events; i++) {
        if (events[i].year != today_yr || events[i].month != today_mo ||
            events[i].day  != today_dy) continue;
        int ev_min = (events[i].hhmm / 100) * 60 + (events[i].hhmm % 100);
        int ex = mtp(ev_min), ey = htp(events[i].height);
        dot_dsc.bg_color = events[i].isHigh ? lv_color_hex(0xFF7B50) : lv_color_hex(0x79C0FF);
        lv_canvas_draw_rect(ti_canvas, ex - 5, ey - 5, 10, 10, &dot_dsc);
        lbl_dsc.color = events[i].isHigh ? lv_color_hex(0xFF7B50) : lv_color_hex(0x79C0FF);
        char lb[12]; snprintf(lb, sizeof(lb), "%s %.2fm", events[i].isHigh ? "H" : "L", events[i].height);
        int lx = ex - 18; if (lx < PX) lx = PX; if (lx + 58 > PX + PW) lx = PX + PW - 58;
        int ly = events[i].isHigh ? ey - 22 : ey + 8;
        lv_canvas_draw_text(ti_canvas, lx, ly, 62, &lbl_dsc, lb);
    }

    if (cur_min >= 0 && cur_min < 1440) {
        int cx = mtp(cur_min);
        float cur_h = interpolate_tide(events, n_events, today_yr, today_mo, today_dy, cur_min);
        int cy = htp(cur_h);
        line_dsc.color = lv_color_hex(0xF85149); line_dsc.width = 1;
        lv_point_t vl[2] = {{(lv_coord_t)cx,(lv_coord_t)PY},{(lv_coord_t)cx,(lv_coord_t)(PY+PH)}};
        lv_canvas_draw_line(ti_canvas, vl, 2, &line_dsc);
        dot_dsc.bg_color = lv_color_hex(0xF85149);
        lv_canvas_draw_rect(ti_canvas, cx - 4, cy - 4, 8, 8, &dot_dsc);
    }

    lv_obj_invalidate(ti_canvas);
}

static void build_tides_screen() {
    lv_obj_t *scr = screens[SCREEN_TIDES];
    build_statusbar(scr, "Tides - Fort Denison, Sydney Harbour");

    const int CY = STATUSBAR_H + PAD;
    const int CH = SCREEN_H - STATUSBAR_H - PAD * 2;
    const int LX = NAV_BTN_W + PAD;
    const int LW = TI_LEFT_CARD_W;
    const int MX = LX + LW + PAD;
    const int MW = 520;
    const int RX = MX + MW + PAD;
    const int RW = SCREEN_W - NAV_BTN_W - PAD - RX;

    // ── Nav buttons (same pattern as all other screens) ───────────────────────
    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);

    // ── Left card ─────────────────────────────────────────────────────────────
    lv_obj_t *lc = lv_obj_create(scr);
    lv_obj_set_size(lc, LW, CH);
    lv_obj_set_pos(lc, LX, CY);
    lv_obj_set_style_bg_color(lc, C_CARD, 0);
    lv_obj_set_style_border_color(lc, C_BORDER, 0);
    lv_obj_set_style_border_width(lc, 1, 0);
    lv_obj_set_style_radius(lc, 12, 0);
    lv_obj_set_style_pad_all(lc, PAD, 0);
    lv_obj_clear_flag(lc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    auto mk_lbl = [](lv_obj_t *par, const char *txt, const lv_font_t *font,
                     lv_color_t col, int x, int y) -> lv_obj_t* {
        lv_obj_t *l = lv_label_create(par);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, col, 0);
        lv_obj_set_pos(l, x, y);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        return l;
    };

    mk_lbl(lc, "NEXT TIDE",  &lv_font_montserrat_14, C_DIM,   0, 0);
    ti_next_type   = mk_lbl(lc, "--", &lv_font_montserrat_28, C_TEXT,   0, 24);
    ti_next_height = mk_lbl(lc, "--", &lv_font_montserrat_40, C_ACCENT, 0, 56);
    ti_next_time   = mk_lbl(lc, "--", &lv_font_montserrat_20, C_TEXT,   0, 106);
    ti_next_in     = mk_lbl(lc, "--", &lv_font_montserrat_16, C_DIM,    0, 132);

    lv_obj_t *div = lv_obj_create(lc);
    lv_obj_set_size(div, LW - PAD * 2, 1);
    lv_obj_set_pos(div, 0, 164);
    lv_obj_set_style_bg_color(div, C_BORDER, 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_clear_flag(div, LV_OBJ_FLAG_CLICKABLE);

    mk_lbl(lc, "AFTER THAT", &lv_font_montserrat_14, C_DIM,   0, 176);
    ti_after_type   = mk_lbl(lc, "--", &lv_font_montserrat_20, C_TEXT,   0, 198);
    ti_after_height = mk_lbl(lc, "--", &lv_font_montserrat_28, C_ACCENT, 0, 222);
    ti_after_time   = mk_lbl(lc, "--", &lv_font_montserrat_16, C_TEXT,   0, 258);
    ti_after_in     = mk_lbl(lc, "--", &lv_font_montserrat_14, C_DIM,    0, 280);

    lv_obj_t *bar_track = lv_obj_create(lc);
    lv_obj_set_size(bar_track, LW - PAD * 2, 12);
    lv_obj_set_pos(bar_track, 0, CH - PAD * 2 - 68);
    lv_obj_set_style_bg_color(bar_track, lv_color_hex(0x21262D), 0);
    lv_obj_set_style_bg_opa(bar_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar_track, 0, 0);
    lv_obj_set_style_radius(bar_track, 6, 0);
    lv_obj_set_style_pad_all(bar_track, 0, 0);
    lv_obj_clear_flag(bar_track, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    ti_dir_bar_fill = lv_obj_create(lc);
    lv_obj_set_size(ti_dir_bar_fill, 0, 12);
    lv_obj_set_pos(ti_dir_bar_fill, 0, CH - PAD * 2 - 68);
    lv_obj_set_style_bg_color(ti_dir_bar_fill, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(ti_dir_bar_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ti_dir_bar_fill, 0, 0);
    lv_obj_set_style_radius(ti_dir_bar_fill, 6, 0);
    lv_obj_set_style_pad_all(ti_dir_bar_fill, 0, 0);
    lv_obj_clear_flag(ti_dir_bar_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    ti_dir_label  = mk_lbl(lc, "--", &lv_font_montserrat_16, C_DIM,  0, CH - PAD * 2 - 48);
    ti_cur_height = mk_lbl(lc, "--", &lv_font_montserrat_16, C_TEXT, 0, CH - PAD * 2 - 24);
    ti_range      = mk_lbl(lc, "--", &lv_font_montserrat_14, C_DIM,  0, CH - PAD * 2);
    mk_lbl(lc, "Station: Fort Denison", &lv_font_montserrat_14, C_DIM, 0, CH - PAD * 2 + 20);

    // ── Centre card ───────────────────────────────────────────────────────────
    lv_obj_t *mc = lv_obj_create(scr);
    lv_obj_set_size(mc, MW, CH);
    lv_obj_set_pos(mc, MX, CY);
    lv_obj_set_style_bg_color(mc, C_CARD, 0);
    lv_obj_set_style_border_color(mc, C_BORDER, 0);
    lv_obj_set_style_border_width(mc, 1, 0);
    lv_obj_set_style_radius(mc, 12, 0);
    lv_obj_set_style_pad_all(mc, PAD, 0);
    lv_obj_clear_flag(mc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    size_t buf_sz = TI_CANVAS_W * TI_CANVAS_H * sizeof(lv_color_t);
    ti_canvas_buf = (lv_color_t *)heap_caps_malloc(buf_sz,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ti_canvas_buf) {
        ti_canvas = lv_canvas_create(mc);
        lv_canvas_set_buffer(ti_canvas, ti_canvas_buf,
                             TI_CANVAS_W, TI_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(ti_canvas, 0, 0);
        lv_canvas_fill_bg(ti_canvas, lv_color_hex(0x0D1117), LV_OPA_COVER);
    }

    // Info rows below canvas — reuse ti_cur_height / ti_range pointers
    // (these are separate label objects to the left card ones — same pointer,
    //  overwritten here intentionally: centre card is the primary display)
    ti_cur_height = mk_lbl(mc, "--", &lv_font_montserrat_16, C_TEXT, 0, TI_CANVAS_H + 10);
    ti_range      = mk_lbl(mc, "--", &lv_font_montserrat_14, C_DIM,  0, TI_CANVAS_H + 34);
    mk_lbl(mc, "Source: BOM Fort Denison IDO59001",
           &lv_font_montserrat_14, C_DIM, 0, TI_CANVAS_H + 56);

    // ── Right card ────────────────────────────────────────────────────────────
    lv_obj_t *rc = lv_obj_create(scr);
    lv_obj_set_size(rc, RW, CH);
    lv_obj_set_pos(rc, RX, CY);
    lv_obj_set_style_bg_color(rc, C_CARD, 0);
    lv_obj_set_style_border_color(rc, C_BORDER, 0);
    lv_obj_set_style_border_width(rc, 1, 0);
    lv_obj_set_style_radius(rc, 12, 0);
    lv_obj_set_style_pad_all(rc, PAD, 0);
    lv_obj_clear_flag(rc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    mk_lbl(rc, "TODAY",    &lv_font_montserrat_14, C_DIM, 0, 0);
    ti_today_list = lv_label_create(rc);
    lv_label_set_text(ti_today_list, "--");
    lv_obj_set_style_text_font(ti_today_list, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ti_today_list, C_TEXT, 0);
    lv_obj_set_pos(ti_today_list, 0, 22);
    lv_label_set_long_mode(ti_today_list, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ti_today_list, RW - PAD * 2);

    mk_lbl(rc, "TOMORROW", &lv_font_montserrat_14, C_DIM, 0, CH / 2 - 20);
    ti_tmrw_list = lv_label_create(rc);
    lv_label_set_text(ti_tmrw_list, "--");
    lv_obj_set_style_text_font(ti_tmrw_list, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ti_tmrw_list, C_TEXT, 0);
    lv_obj_set_pos(ti_tmrw_list, 0, CH / 2);
    lv_label_set_long_mode(ti_tmrw_list, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ti_tmrw_list, RW - PAD * 2);

    ti_nosd_lbl = lv_label_create(scr);
    lv_label_set_text(ti_nosd_lbl, LV_SYMBOL_DRIVE "  No SD card - tides unavailable\n"
                                   "Copy tides.csv to root of SD card");
    lv_obj_set_style_text_font(ti_nosd_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ti_nosd_lbl, C_DIM, 0);
    lv_obj_align(ti_nosd_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(ti_nosd_lbl, LV_OBJ_FLAG_HIDDEN);
}

void ui_update_tides(const RtcDateTime &dt) {
    if (!ti_next_type) return;

    if (!sd_available()) {
        if (ti_nosd_lbl) lv_obj_clear_flag(ti_nosd_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (ti_nosd_lbl) lv_obj_add_flag(ti_nosd_lbl, LV_OBJ_FLAG_HIDDEN);

    int today_yr = dt.year, today_mo = dt.month, today_dy = dt.day;
    struct tm t = {};
    t.tm_year = today_yr - 1900; t.tm_mon = today_mo - 1; t.tm_mday = today_dy + 1;
    t.tm_isdst = -1;
    time_t tmrw_e = mktime(&t);
    struct tm *tmrw = localtime(&tmrw_e);
    int tmrw_yr = tmrw->tm_year + 1900, tmrw_mo = tmrw->tm_mon + 1, tmrw_dy = tmrw->tm_mday;
    int cur_min = dt.hour * 60 + dt.minute;

    TideEvent events[TIDE_MAX_EVENTS];
    int n = load_tide_events(today_yr, today_mo, today_dy,
                             tmrw_yr, tmrw_mo, tmrw_dy,
                             events, TIDE_MAX_EVENTS);
    if (n == 0) {
        lv_label_set_text(ti_next_type,   "No data");
        lv_label_set_text(ti_next_height, "--");
        lv_label_set_text(ti_next_time,   "Check /tides.csv on SD");
        lv_label_set_text(ti_next_in,     "");
        return;
    }

    int next_idx = -1, after_idx = -1;
    for (int i = 0; i < n; i++) {
        int ev_min = (events[i].hhmm / 100) * 60 + (events[i].hhmm % 100);
        bool is_today = (events[i].year == today_yr && events[i].month == today_mo &&
                         events[i].day  == today_dy);
        bool is_tmrw  = (events[i].year == tmrw_yr  && events[i].month == tmrw_mo  &&
                         events[i].day  == tmrw_dy);
        bool upcoming = (is_today && ev_min > cur_min) || is_tmrw;
        if (upcoming && next_idx  < 0) { next_idx  = i; continue; }
        if (upcoming && after_idx < 0) { after_idx = i; break; }
    }

    char buf[64];
    auto set_tide_widgets = [&](lv_obj_t *type_lbl, lv_obj_t *ht_lbl,
                                lv_obj_t *time_lbl, lv_obj_t *in_lbl, int idx) {
        if (idx < 0) return;
        const TideEvent &ev = events[idx];
        int hh = ev.hhmm / 100, mm = ev.hhmm % 100;
        int ev_min = hh * 60 + mm;
        bool is_tmrw_ev = (ev.year == tmrw_yr && ev.month == tmrw_mo && ev.day == tmrw_dy);
        int mins_away = is_tmrw_ev ? (1440 - cur_min + ev_min) : (ev_min - cur_min);
        lv_color_t col = ev.isHigh ? lv_color_hex(0xFF7B50) : lv_color_hex(0x79C0FF);
        lv_label_set_text(type_lbl, ev.isHigh ? LV_SYMBOL_UP " HIGH" : LV_SYMBOL_DOWN " LOW");
        lv_obj_set_style_text_color(type_lbl, col, 0);
        snprintf(buf, sizeof(buf), "%.2fm", ev.height);
        lv_label_set_text(ht_lbl, buf);
        lv_obj_set_style_text_color(ht_lbl, col, 0);
        int disp_h = hh > 12 ? hh - 12 : (hh == 0 ? 12 : hh);
        snprintf(buf, sizeof(buf), "%d:%02d %s%s", disp_h, mm,
                 hh >= 12 ? "PM" : "AM", is_tmrw_ev ? " (tmrw)" : "");
        lv_label_set_text(time_lbl, buf);
        snprintf(buf, sizeof(buf), "in %dh %02dm", mins_away / 60, mins_away % 60);
        lv_label_set_text(in_lbl, buf);
    };

    set_tide_widgets(ti_next_type,  ti_next_height,  ti_next_time,  ti_next_in,  next_idx);
    set_tide_widgets(ti_after_type, ti_after_height, ti_after_time, ti_after_in, after_idx);

    float cur_h = interpolate_tide(events, n, today_yr, today_mo, today_dy, cur_min);
    float h_lo = 99.0f, h_hi = -99.0f;
    for (int i = 0; i < n; i++) {
        if (events[i].year != today_yr || events[i].month != today_mo ||
            events[i].day  != today_dy) continue;
        if (events[i].height < h_lo) h_lo = events[i].height;
        if (events[i].height > h_hi) h_hi = events[i].height;
    }
    bool rising = (next_idx >= 0 && events[next_idx].isHigh);
    snprintf(buf, sizeof(buf), "Current: %.2fm  %s",
             cur_h, rising ? LV_SYMBOL_UP " Rising" : LV_SYMBOL_DOWN " Falling");
    lv_label_set_text(ti_cur_height, buf);
    snprintf(buf, sizeof(buf), "Range today: %.2fm",
             (h_hi > h_lo && h_hi < 90.0f) ? (h_hi - h_lo) : 0.0f);
    lv_label_set_text(ti_range, buf);

    if (ti_dir_bar_fill && h_hi > h_lo) {
        const int BAR_W = TI_LEFT_CARD_W - PAD * 2;
        float frac = (cur_h - h_lo) / (h_hi - h_lo);
        frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
        lv_obj_set_width(ti_dir_bar_fill, (int)(frac * BAR_W));
        lv_obj_set_style_bg_color(ti_dir_bar_fill,
            rising ? lv_color_hex(0xFF7B50) : lv_color_hex(0x79C0FF), 0);
    }
    if (ti_dir_label)
        lv_label_set_text(ti_dir_label,
            rising ? LV_SYMBOL_UP " Rising" : LV_SYMBOL_DOWN " Falling");

    // Today list — LVGL recolor markup: #RRGGBB text#
    lv_label_set_recolor(ti_today_list, true);
    char today_buf[512] = "";
    for (int i = 0; i < n; i++) {
        if (events[i].year != today_yr || events[i].month != today_mo ||
            events[i].day  != today_dy) continue;
        // High = orange FF7B50, Low = blue 79C0FF
        const char *hex = events[i].isHigh ? "FF7B50" : "79C0FF";
        char row[80];
        snprintf(row, sizeof(row), "#%s %s %02d:%02d  %.2fm#\n",
                 hex,
                 events[i].isHigh ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                 events[i].hhmm / 100, events[i].hhmm % 100,
                 events[i].height);
        strncat(today_buf, row, sizeof(today_buf) - strlen(today_buf) - 1);
    }
    lv_label_set_text(ti_today_list, today_buf[0] ? today_buf : "--");

    // Tomorrow list
    lv_label_set_recolor(ti_tmrw_list, true);
    char tmrw_buf[512] = "";
    for (int i = 0; i < n; i++) {
        if (events[i].year != tmrw_yr || events[i].month != tmrw_mo ||
            events[i].day  != tmrw_dy) continue;
        const char *hex = events[i].isHigh ? "FF7B50" : "79C0FF";
        char row[80];
        snprintf(row, sizeof(row), "#%s %s %02d:%02d  %.2fm#\n",
                 hex,
                 events[i].isHigh ? LV_SYMBOL_UP : LV_SYMBOL_DOWN,
                 events[i].hhmm / 100, events[i].hhmm % 100,
                 events[i].height);
        strncat(tmrw_buf, row, sizeof(tmrw_buf) - strlen(tmrw_buf) - 1);
    }
    lv_label_set_text(ti_tmrw_list, tmrw_buf[0] ? tmrw_buf : "--");

    draw_tide_canvas(events, n, today_yr, today_mo, today_dy, cur_min);
}

// =============================================================================
//  SCREEN 13 — Earthquake Activity (Australia / Pacific)
// =============================================================================

// Layout constants
#define EQ_MAP_CANVAS_W   800    // map canvas width on screen
#define EQ_MAP_CANVAS_H   540    // map canvas height on screen
#define EQ_RIGHT_PANEL_W  270    // right panel width

// Statics
static lv_obj_t    *eq_canvas      = nullptr;
static lv_color_t  *eq_buf         = nullptr;
static lv_obj_t    *eq_filter_7d   = nullptr;
static lv_obj_t    *eq_filter_24h  = nullptr;
static lv_obj_t    *eq_filter_m4   = nullptr;
static lv_obj_t    *eq_largest_mag = nullptr;
static lv_obj_t    *eq_largest_place = nullptr;
static lv_obj_t    *eq_largest_depth = nullptr;
static lv_obj_t    *eq_largest_age   = nullptr;
static lv_obj_t    *eq_largest_dist  = nullptr;
static lv_obj_t    *eq_sum_total   = nullptr;
static lv_obj_t    *eq_sum_m6      = nullptr;
static lv_obj_t    *eq_sum_m5      = nullptr;
static lv_obj_t    *eq_sum_m4      = nullptr;
static lv_obj_t    *eq_list_labels[8] = {};  // up to 8 list rows
static lv_obj_t    *eq_updated_lbl = nullptr;
static int          eq_filter_window = 0;   // 0=7d 1=24h
static bool         eq_filter_m4plus = false;

// ── Draw the coastline + earthquake dots onto the map canvas ─────────────────
static void eq_draw_map(const EqResult &eq) {
    if (!eq_canvas || !eq_buf) return;

    lv_obj_clean(eq_canvas);
    lv_canvas_set_buffer(eq_canvas, eq_buf,
                         EQ_MAP_CANVAS_W, EQ_MAP_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(eq_canvas, lv_color_hex(0x0a1628), LV_OPA_COVER);

    const lv_color_t GRID_COL  = lv_color_hex(0x111827);
    const lv_color_t COAST_COL = lv_color_hex(0x2d5272);
    const lv_color_t LABEL_COL = lv_color_hex(0x1e3a50);

    // ── Grid lines ────────────────────────────────────────────────────────────
    for (int lon = 110; lon <= 180; lon += 10) {
        int x = EQ_LON_TO_X(lon);
        canvas_line(eq_canvas, x, 0, x, EQ_MAP_CANVAS_H, GRID_COL, 1);
    }
    for (int lat = -50; lat <= 20; lat += 10) {
        int y = EQ_LAT_TO_Y(lat);
        canvas_line(eq_canvas, 0, y, EQ_MAP_CANVAS_W, y, GRID_COL, 1);
    }

    // Grid labels — lon at top, lat on left
    lv_draw_label_dsc_t ldsc;
    lv_draw_label_dsc_init(&ldsc);
    ldsc.font  = &lv_font_montserrat_14;
    ldsc.color = LABEL_COL;
    char lbl[8];

    for (int lon = 110; lon <= 180; lon += 10) {
        int x = EQ_LON_TO_X(lon);
        snprintf(lbl, sizeof(lbl), "%d", lon);
        lv_canvas_draw_text(eq_canvas, x + 2, 2, 36, &ldsc, lbl);
    }
    for (int lat = -50; lat <= 20; lat += 10) {
        int y = EQ_LAT_TO_Y(lat);
        snprintf(lbl, sizeof(lbl), "%d", lat);
        lv_canvas_draw_text(eq_canvas, 2, y - 14, 30, &ldsc, lbl);
    }

    // ── Coastlines ────────────────────────────────────────────────────────────
    for (int s = 0; s < EQ_COAST_SEGS; s++) {
        int start = eq_coast_seg_start[s];
        int len   = eq_coast_seg_len[s];
        for (int i = 0; i < len - 1; i++) {
            int x0 = eq_coast_pts[start + i][0];
            int y0 = eq_coast_pts[start + i][1];
            int x1 = eq_coast_pts[start + i + 1][0];
            int y1 = eq_coast_pts[start + i + 1][1];
            // Skip segments that cross a large gap (island boundary artefact)
            int dx = x1-x0, dy = y1-y0;
            if (dx*dx + dy*dy > 10000) continue;  // >100px jump = skip
            // Clip to canvas
            if (x0<0||x0>=EQ_MAP_CANVAS_W||y0<0||y0>=EQ_MAP_CANVAS_H) continue;
            if (x1<0||x1>=EQ_MAP_CANVAS_W||y1<0||y1>=EQ_MAP_CANVAS_H) continue;
            canvas_line(eq_canvas, x0, y0, x1, y1, COAST_COL, 1);
        }
    }

    // ── Earthquake dots ───────────────────────────────────────────────────────
    if (eq.valid) {
        int64_t cutoff_min = (eq_filter_window == 1) ? 24*60LL : 7*24*60LL;

        for (int i = 0; i < eq.count; i++) {
            const EqEvent &ev = eq.events[i];

            // Time filter
            if (ev.age_minutes >= 0 && ev.age_minutes > (int32_t)cutoff_min) continue;
            // Magnitude filter
            if (eq_filter_m4plus && ev.mag < 4.0f) continue;

            int cx = EQ_LON_TO_X(ev.lon);
            int cy = EQ_LAT_TO_Y(ev.lat);
            if (cx < 2 || cx >= EQ_MAP_CANVAS_W-2 || cy < 2 || cy >= EQ_MAP_CANVAS_H-2) continue;

            // Colour by depth
            lv_color_t dot_col;
            if (ev.depth_km < 70.0f)       dot_col = lv_color_hex(0xF85149);
            else if (ev.depth_km < 300.0f)  dot_col = lv_color_hex(0xFF7B50);
            else                             dot_col = lv_color_hex(0xD29922);

            // Radius by magnitude: 2.5=3px .. 7.0=15px
            int r = (int)((ev.mag - 2.5f) * 2.2f) + 3;
            if (r < 3)  r = 3;
            if (r > 15) r = 15;

            // Pulse ring for largest
            if (i == eq.largest_idx) {
                lv_color_t ring = lv_color_mix(dot_col, lv_color_hex(0x0a1628), 80);
                canvas_fill_circle(eq_canvas, cx, cy, r + 6, ring);
            }

            canvas_fill_circle(eq_canvas, cx, cy, r, dot_col);

            // Magnitude label on M5.5+ dots
            if (ev.mag >= 5.5f && r >= 7) {
                lv_draw_label_dsc_t mdsc;
                lv_draw_label_dsc_init(&mdsc);
                mdsc.color = lv_color_hex(0xFFFFFF);
                mdsc.font  = &lv_font_montserrat_14;
                char mlbl[8];
                snprintf(mlbl, sizeof(mlbl), "%.1f", ev.mag);
                lv_canvas_draw_text(eq_canvas, cx - r + 1, cy - 7, r * 2, &mdsc, mlbl);
            }
        }
    }

    // ── Sydney reference dot ──────────────────────────────────────────────────
    int syd_x = EQ_LON_TO_X(SYDNEY_LON);
    int syd_y = EQ_LAT_TO_Y(SYDNEY_LAT);
    canvas_fill_circle(eq_canvas, syd_x, syd_y, 3, lv_color_hex(0x58A6FF));
    lv_draw_label_dsc_t syd_dsc;
    lv_draw_label_dsc_init(&syd_dsc);
    syd_dsc.color = lv_color_hex(0x58A6FF);
    syd_dsc.font  = &lv_font_montserrat_14;
    lv_canvas_draw_text(eq_canvas, syd_x + 5, syd_y - 8, 60, &syd_dsc, "Sydney");

    // ── Depth legend ──────────────────────────────────────────────────────────
    // Small coloured dots + labels at bottom-left of canvas
    struct { lv_color_t col; const char *lbl; } legend[] = {
        { lv_color_hex(0xF85149), "Shallow <70km" },
        { lv_color_hex(0xFF7B50), "Intermediate"  },
        { lv_color_hex(0xD29922), "Deep >300km"   },
    };
    lv_draw_label_dsc_t leg_dsc;
    lv_draw_label_dsc_init(&leg_dsc);
    leg_dsc.font  = &lv_font_montserrat_14;
    leg_dsc.color = lv_color_hex(0x8B949E);
    for (int i = 0; i < 3; i++) {
        canvas_fill_circle(eq_canvas, 12, EQ_MAP_CANVAS_H - 42 + i * 14, 4, legend[i].col);
        lv_canvas_draw_text(eq_canvas, 20, EQ_MAP_CANVAS_H - 49 + i * 14, 130, &leg_dsc, legend[i].lbl);
    }

    lv_obj_invalidate(eq_canvas);
}

// ── Cached last result — so filter buttons can redraw immediately ─────────────
static EqResult eq_cached = {};

// ── Filter button callbacks ───────────────────────────────────────────────────
static void eq_filter_7d_cb(lv_event_t *e) {
    eq_filter_window = 0;
    lv_obj_set_style_bg_color(eq_filter_7d,  lv_color_hex(0x1C3A5E), 0);
    lv_obj_set_style_border_color(eq_filter_7d,  C_ACCENT, 0);
    lv_obj_set_style_text_color(eq_filter_7d,  C_ACCENT, 0);
    lv_obj_set_style_bg_color(eq_filter_24h, C_BG, 0);
    lv_obj_set_style_border_color(eq_filter_24h, C_BORDER, 0);
    lv_obj_set_style_text_color(eq_filter_24h, C_DIM, 0);
    eq_draw_map(eq_cached);   // redraw immediately with new filter
}

static void eq_filter_24h_cb(lv_event_t *e) {
    eq_filter_window = 1;
    lv_obj_set_style_bg_color(eq_filter_24h, lv_color_hex(0x1C3A5E), 0);
    lv_obj_set_style_border_color(eq_filter_24h, C_ACCENT, 0);
    lv_obj_set_style_text_color(eq_filter_24h, C_ACCENT, 0);
    lv_obj_set_style_bg_color(eq_filter_7d,  C_BG, 0);
    lv_obj_set_style_border_color(eq_filter_7d,  C_BORDER, 0);
    lv_obj_set_style_text_color(eq_filter_7d,  C_DIM, 0);
    eq_draw_map(eq_cached);   // redraw immediately with new filter
}

static void eq_filter_m4_cb(lv_event_t *e) {
    eq_filter_m4plus = !eq_filter_m4plus;
    if (eq_filter_m4plus) {
        lv_obj_set_style_bg_color(eq_filter_m4, lv_color_hex(0x1C3A5E), 0);
        lv_obj_set_style_border_color(eq_filter_m4, C_ACCENT, 0);
        lv_obj_set_style_text_color(eq_filter_m4, C_ACCENT, 0);
    } else {
        lv_obj_set_style_bg_color(eq_filter_m4, C_BG, 0);
        lv_obj_set_style_border_color(eq_filter_m4, C_BORDER, 0);
        lv_obj_set_style_text_color(eq_filter_m4, C_DIM, 0);
    }
    eq_draw_map(eq_cached);   // redraw immediately with new filter
}

// ── Build earthquake screen ───────────────────────────────────────────────────
static void build_earthquake_screen() {
    lv_obj_t *scr = screens[SCREEN_EARTHQUAKE];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, "Earthquake Activity - Australia / Pacific");

    const int CY  = STATUSBAR_H;
    const int CH  = SCREEN_H - STATUSBAR_H - 26;  // 650
    const int NAV = NAV_BTN_W;
    const int RP  = EQ_RIGHT_PANEL_W;             // right panel
    const int GP  = 8;                            // gap

    // Content area
    const int BODY_X = NAV;
    const int BODY_W = SCREEN_W - NAV * 2;
    const int BODY_Y = CY + 8;
    const int BODY_H = CH - 8;

    // Map card (left)
    const int MAP_CARD_W = BODY_W - RP - GP;
    lv_obj_t *map_card = make_card_nopad(scr, BODY_X, BODY_Y, MAP_CARD_W, BODY_H);

    // Allocate map canvas buffer in PSRAM
    size_t buf_sz = (size_t)EQ_MAP_CANVAS_W * EQ_MAP_CANVAS_H * sizeof(lv_color_t);
    eq_buf = (lv_color_t *)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!eq_buf) eq_buf = (lv_color_t *)malloc(buf_sz);

    eq_canvas = lv_canvas_create(map_card);
    lv_canvas_set_buffer(eq_canvas, eq_buf,
                         EQ_MAP_CANVAS_W, EQ_MAP_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(eq_canvas, MAP_CARD_W, BODY_H);
    lv_obj_align(eq_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(eq_canvas, lv_color_hex(0x0a1628), LV_OPA_COVER);

    // Draw initial coastlines (no earthquake data yet)
    {
        EqResult empty = {};
        eq_draw_map(empty);
    }

    // ── Right panel ───────────────────────────────────────────────────────────
    const int RX   = BODY_X + MAP_CARD_W + GP;
    const int CARD_GP = 6;
    int ry = BODY_Y;

    // Filter buttons row
    auto make_fbtn = [&](const char *lbl, int x, int w, lv_event_cb_t cb) -> lv_obj_t* {
        lv_obj_t *b = lv_btn_create(scr);
        lv_obj_set_pos(b, x, ry);
        lv_obj_set_size(b, w, 36);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_bg_color(b, C_BG, 0);
        lv_obj_set_style_border_color(b, C_BORDER, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_t *lv = lv_label_create(b);
        lv_label_set_text(lv, lbl);
        lv_obj_set_style_text_font(lv, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lv, C_DIM, 0);
        lv_obj_center(lv);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        return b;
    };

    int fbw = (RP - GP * 2) / 3;
    eq_filter_7d  = make_fbtn("7d",   RX,              fbw, eq_filter_7d_cb);
    eq_filter_24h = make_fbtn("24h",  RX + fbw + GP,   fbw, eq_filter_24h_cb);
    eq_filter_m4  = make_fbtn("M4+",  RX + fbw*2+GP*2, fbw, eq_filter_m4_cb);

    // Default: 7d active
    lv_obj_set_style_bg_color(eq_filter_7d, lv_color_hex(0x1C3A5E), 0);
    lv_obj_set_style_border_color(eq_filter_7d, C_ACCENT, 0);
    lv_obj_set_style_text_color(eq_filter_7d, C_ACCENT, 0);

    ry += 36 + CARD_GP;

    // Largest event card
    const int LARGE_CARD_H = 140;
    lv_obj_t *large_card = make_card_nopad(scr, RX, ry, RP, LARGE_CARD_H);
    lv_obj_set_style_border_color(large_card, lv_color_hex(0xF85149), 0);
    lv_obj_set_style_pad_all(large_card, 10, 0);
    make_label(large_card, "LARGEST RECENT", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    eq_largest_mag = lv_label_create(large_card);
    lv_label_set_text(eq_largest_mag, "--");
    lv_obj_set_style_text_font(eq_largest_mag, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(eq_largest_mag, lv_color_hex(0xF85149), 0);
    lv_obj_align(eq_largest_mag, LV_ALIGN_TOP_LEFT, 0, 18);

    eq_largest_place = lv_label_create(large_card);
    lv_label_set_text(eq_largest_place, "--");
    lv_obj_set_style_text_font(eq_largest_place, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(eq_largest_place, C_TEXT, 0);
    lv_obj_align(eq_largest_place, LV_ALIGN_TOP_LEFT, 0, 52);
    lv_label_set_long_mode(eq_largest_place, LV_LABEL_LONG_DOT);
    lv_obj_set_width(eq_largest_place, RP - 20);

    eq_largest_depth = lv_label_create(large_card);
    lv_label_set_text(eq_largest_depth, "");
    lv_obj_set_style_text_font(eq_largest_depth, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(eq_largest_depth, C_DIM, 0);
    lv_obj_align(eq_largest_depth, LV_ALIGN_TOP_LEFT, 0, 70);

    eq_largest_age = lv_label_create(large_card);
    lv_label_set_text(eq_largest_age, "");
    lv_obj_set_style_text_font(eq_largest_age, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(eq_largest_age, C_DIM, 0);
    lv_obj_align(eq_largest_age, LV_ALIGN_TOP_LEFT, 0, 88);

    eq_largest_dist = lv_label_create(large_card);
    lv_label_set_text(eq_largest_dist, "");
    lv_obj_set_style_text_font(eq_largest_dist, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(eq_largest_dist, C_DIM, 0);
    lv_obj_align(eq_largest_dist, LV_ALIGN_TOP_LEFT, 0, 106);

    ry += LARGE_CARD_H + CARD_GP;

    // Summary counts card
    const int SUM_CARD_H = 70;
    lv_obj_t *sum_card = make_card_nopad(scr, RX, ry, RP, SUM_CARD_H);
    lv_obj_set_style_pad_all(sum_card, 8, 0);
    make_label(sum_card, "PAST 7 DAYS", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    // 4 count columns: total / M6+ / M5+ / M4+
    struct { lv_obj_t **ptr; const char *lbl; lv_color_t col; int xoff; } cols[] = {
        {&eq_sum_total, "total", C_TEXT,                    0},
        {&eq_sum_m6,    "M6+",  lv_color_hex(0xF85149),   60},
        {&eq_sum_m5,    "M5+",  lv_color_hex(0xFF7B50),  120},
        {&eq_sum_m4,    "M4+",  lv_color_hex(0xD29922),  185},
    };
    for (auto &col : cols) {
        *col.ptr = lv_label_create(sum_card);
        lv_label_set_text(*col.ptr, "--");
        lv_obj_set_style_text_font(*col.ptr, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(*col.ptr, col.col, 0);
        lv_obj_set_pos(*col.ptr, col.xoff, 18);

        lv_obj_t *sub = lv_label_create(sum_card);
        lv_label_set_text(sub, col.lbl);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sub, C_DIM, 0);
        lv_obj_set_pos(sub, col.xoff, 42);
    }

    ry += SUM_CARD_H + CARD_GP;

    // Recent events list
    const int EQ_LIST_H = BODY_Y + BODY_H - ry;
    lv_obj_t *list_card = make_card_nopad(scr, RX, ry, RP, EQ_LIST_H);
    lv_obj_set_style_pad_all(list_card, 8, 0);
    make_label(list_card, "RECENT EVENTS", &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_LEFT, 0, 0);

    const int ROW_H = (EQ_LIST_H - 24) / 8;
    for (int i = 0; i < 8; i++) {
        eq_list_labels[i] = lv_label_create(list_card);
        lv_label_set_text(eq_list_labels[i], "");
        lv_obj_set_style_text_font(eq_list_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(eq_list_labels[i], C_DIM, 0);
        lv_obj_align(eq_list_labels[i], LV_ALIGN_TOP_LEFT, 0, 20 + i * ROW_H);
        lv_label_set_long_mode(eq_list_labels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(eq_list_labels[i], RP - 16);
    }

    // Updated label at bottom of map card
    eq_updated_lbl = lv_label_create(scr);
    lv_label_set_text(eq_updated_lbl, "");
    lv_obj_set_style_text_font(eq_updated_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(eq_updated_lbl, C_BORDER, 0);
    lv_obj_set_pos(eq_updated_lbl, BODY_X + 6, BODY_Y + BODY_H - 18);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  UPDATE: EARTHQUAKE SCREEN
// =============================================================================
void ui_update_earthquake(const EqResult &eq) {
    if (!eq_canvas) return;

    // Cache so filter button callbacks can redraw immediately without a new fetch
    eq_cached = eq;

    // Redraw map
    eq_draw_map(eq);

    if (!eq.valid) {
        lv_label_set_text(eq_largest_mag, "--");
        lv_label_set_text(eq_largest_place, "No data");
        lv_label_set_text(eq_updated_lbl, "No data");
        return;
    }

    char buf[64];

    // ── Largest event ─────────────────────────────────────────────────────────
    if (eq.count > 0) {
        const EqEvent &ev = eq.events[eq.largest_idx];

        snprintf(buf, sizeof(buf), "M %.1f", ev.mag);
        lv_label_set_text(eq_largest_mag, buf);

        // Colour largest mag by depth
        lv_color_t mc = (ev.depth_km < 70)   ? lv_color_hex(0xF85149) :
                        (ev.depth_km < 300)   ? lv_color_hex(0xFF7B50) :
                                                lv_color_hex(0xD29922);
        lv_obj_set_style_text_color(eq_largest_mag, mc, 0);

        lv_label_set_text(eq_largest_place, ev.place);

        snprintf(buf, sizeof(buf), "Depth: %.0f km  |  %s",
                 ev.depth_km, eq_depth_label(ev.depth_km));
        lv_label_set_text(eq_largest_depth, buf);

        if (ev.age_minutes >= 0) {
            int h = ev.age_minutes / 60, m = ev.age_minutes % 60;
            if (h == 0)       snprintf(buf, sizeof(buf), "%dm ago", m);
            else if (h < 48)  snprintf(buf, sizeof(buf), "%dh %dm ago", h, m);
            else              snprintf(buf, sizeof(buf), "%dd ago", h/24);
        } else {
            snprintf(buf, sizeof(buf), "Time unknown");
        }
        lv_label_set_text(eq_largest_age, buf);

        if (ev.dist_km > 0)
            snprintf(buf, sizeof(buf), "%d km from Sydney", ev.dist_km);
        else
            snprintf(buf, sizeof(buf), "");
        lv_label_set_text(eq_largest_dist, buf);
    } else {
        lv_label_set_text(eq_largest_mag,   "None");
        lv_label_set_text(eq_largest_place, "No events in region");
        lv_label_set_text(eq_largest_depth, "");
        lv_label_set_text(eq_largest_age,   "");
        lv_label_set_text(eq_largest_dist,  "");
    }

    // ── Summary counts ────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%d",  eq.count);         lv_label_set_text(eq_sum_total, buf);
    snprintf(buf, sizeof(buf), "%d",  eq.count_m6plus);  lv_label_set_text(eq_sum_m6, buf);
    snprintf(buf, sizeof(buf), "%d",  eq.count_m5plus);  lv_label_set_text(eq_sum_m5, buf);
    snprintf(buf, sizeof(buf), "%d",  eq.count_m4plus);  lv_label_set_text(eq_sum_m4, buf);

    // ── Recent events list ────────────────────────────────────────────────────
    for (int i = 0; i < 8; i++) {
        if (i < eq.count) {
            const EqEvent &ev = eq.events[i];
            int h = ev.age_minutes / 60, m = ev.age_minutes % 60;
            char age[12];
            if (ev.age_minutes < 0)     snprintf(age, sizeof(age), "?");
            else if (ev.age_minutes<60) snprintf(age, sizeof(age), "%dm", m);
            else if (h < 48)            snprintf(age, sizeof(age), "%dh", h);
            else                        snprintf(age, sizeof(age), "%dd", h/24);

            snprintf(buf, sizeof(buf), "M%.1f  %s  %s",
                     ev.mag, age, ev.place);
            lv_label_set_text(eq_list_labels[i], buf);

            lv_color_t lc = (ev.mag >= 6.0f) ? lv_color_hex(0xF85149) :
                            (ev.mag >= 5.0f) ? lv_color_hex(0xFF7B50) :
                                               lv_color_hex(0xD29922);
            lv_obj_set_style_text_color(eq_list_labels[i], lc, 0);
        } else {
            lv_label_set_text(eq_list_labels[i], "");
        }
    }

    // ── Updated timestamp ─────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "Updated %s  |  %d events globally",
             eq.fetch_time, eq.total_global);
    lv_label_set_text(eq_updated_lbl, buf);
}

// =============================================================================
//  BUILD: SETTINGS SCREEN
// =============================================================================
static void build_settings_screen() {
    lv_obj_t *scr = screens[SCREEN_SETTINGS];
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    build_statusbar(scr, LV_SYMBOL_SETTINGS "  Settings & System");

    const int CY   = STATUSBAR_H + PAD;
    const int CX_L = NAV_BTN_W + PAD;          // left column start
    const int CX_R = NAV_BTN_W + 560;           // right column start
    const int LEFT_CARD_W = 510;
    const int RIGHT_CARD_W = SCREEN_W - NAV_BTN_W * 2 - LEFT_CARD_W - PAD * 3;
    const int CARD_H = SCREEN_H - STATUSBAR_H - PAD * 2 - 26;
    const int ROW_H  = 68;

    // ── LEFT — Settings card ──────────────────────────────────────────────────
    lv_obj_t *left_card = make_card_nopad(scr, CX_L, CY, LEFT_CARD_W, CARD_H);
    lv_obj_set_style_pad_all(left_card, PAD, 0);
    make_label(left_card, "DISPLAY", &lv_font_montserrat_14, C_DIM,
               LV_ALIGN_TOP_LEFT, 0, 0);

    int ry = 22;  // current row y inside card (below section header)

    // Row divider helper lambda
    auto add_divider = [&]() {
        lv_obj_t *d = lv_obj_create(left_card);
        lv_obj_set_size(d, LEFT_CARD_W - PAD * 2, 1);
        lv_obj_set_pos(d, 0, ry);
        lv_obj_set_style_bg_color(d, lv_color_hex(0x21262D), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        ry += 8;
    };

    // ── Brightness ────────────────────────────────────────────────────────────
    {
        static const char *blbls[BRIGHT_STEPS] = {"30","80","130","180","230"};
        make_setting_row(left_card, ry, ROW_H, "Brightness",
                         blbls, BRIGHT_STEPS, set_bright_btns, set_bright_cb);
        ry += ROW_H;
    }
    add_divider();

    // ── Screen Timeout ────────────────────────────────────────────────────────
    {
        make_setting_row(left_card, ry, ROW_H, "Screen timeout",
                         TIMEOUT_LBLS, TIMEOUT_OPTS, set_timeout_btns, set_timeout_cb);
        ry += ROW_H;
    }
    add_divider();

    // ── Dim before sleep ─────────────────────────────────────────────────────
    {
        static const char *dlbls[2] = {"Dim first","Instant off"};
        lv_obj_t *dim_btns[2] = {};
        make_setting_row(left_card, ry, ROW_H, "Before sleep",
                         dlbls, 2, dim_btns, set_dim_cb);
        set_dim_on  = dim_btns[0];
        set_dim_off = dim_btns[1];
        ry += ROW_H;
    }

    // Section: NETWORK
    ry += 8;
    lv_obj_t *net_hdr = lv_label_create(left_card);
    lv_label_set_text(net_hdr, "FETCH INTERVALS");
    lv_obj_set_style_text_font(net_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(net_hdr, C_DIM, 0);
    lv_obj_set_pos(net_hdr, 0, ry);
    ry += 20;
    add_divider();

    // ── Weather interval ──────────────────────────────────────────────────────
    {
        make_setting_row(left_card, ry, ROW_H, "Weather",
                         WX_LBLS, WX_OPTS, set_wx_btns, set_wx_cb);
        ry += ROW_H;
    }
    add_divider();

    // ── Space weather interval ────────────────────────────────────────────────
    {
        make_setting_row(left_card, ry, ROW_H, "Space weather",
                         SW_LBLS, SW_OPTS, set_sw_btns, set_sw_cb);
        ry += ROW_H;
    }
    add_divider();

    // ── Wake to screen ────────────────────────────────────────────────────────
    {
        static const char *wlbls[2] = {"Last screen","Clock"};
        lv_obj_t *wake_btns[2] = {};
        make_setting_row(left_card, ry, ROW_H, "Wake to",
                         wlbls, 2, wake_btns, set_wake_cb);
        set_wake_last  = wake_btns[0];
        set_wake_clock = wake_btns[1];
        ry += ROW_H;
    }

    // ── Refresh Now buttons (footer) ──────────────────────────────────────────
    ry += PAD;
    {
        const int BTN_W = (LEFT_CARD_W - PAD * 2 - PAD) / 2;
        const int BTN_H = 44;

        lv_obj_t *wx_btn = lv_btn_create(left_card);
        lv_obj_set_size(wx_btn, BTN_W, BTN_H);
        lv_obj_set_pos(wx_btn, 0, ry);
        lv_obj_set_style_bg_color(wx_btn, lv_color_hex(0x1C3A5E), 0);
        lv_obj_set_style_bg_opa(wx_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(wx_btn, C_ACCENT, 0);
        lv_obj_set_style_border_width(wx_btn, 1, 0);
        lv_obj_set_style_radius(wx_btn, 8, 0);
        lv_obj_set_style_shadow_width(wx_btn, 0, 0);
        lv_obj_add_event_cb(wx_btn, refresh_wx_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *wxl = lv_label_create(wx_btn);
        lv_label_set_text(wxl, LV_SYMBOL_REFRESH "  Refresh Weather");
        lv_obj_set_style_text_font(wxl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(wxl, C_ACCENT, 0);
        lv_obj_center(wxl);

        lv_obj_t *sw_btn = lv_btn_create(left_card);
        lv_obj_set_size(sw_btn, BTN_W, BTN_H);
        lv_obj_set_pos(sw_btn, BTN_W + PAD, ry);
        lv_obj_set_style_bg_color(sw_btn, lv_color_hex(0x1C3A5E), 0);
        lv_obj_set_style_bg_opa(sw_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(sw_btn, C_COOL, 0);
        lv_obj_set_style_border_width(sw_btn, 1, 0);
        lv_obj_set_style_radius(sw_btn, 8, 0);
        lv_obj_set_style_shadow_width(sw_btn, 0, 0);
        lv_obj_add_event_cb(sw_btn, refresh_sw_cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *swl = lv_label_create(sw_btn);
        lv_label_set_text(swl, LV_SYMBOL_REFRESH "  Refresh Space Wx");
        lv_obj_set_style_text_font(swl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(swl, C_COOL, 0);
        lv_obj_center(swl);
    }

    // ── RIGHT — System info card ──────────────────────────────────────────────
    lv_obj_t *right_card = make_card_nopad(scr, CX_R, CY, RIGHT_CARD_W, CARD_H);
    lv_obj_set_style_pad_all(right_card, PAD, 0);

    // Helper: make a sysinfo row (label + value)
    int sy = 0;
    auto sys_row = [&](const char *lbl_txt, lv_obj_t **val_ptr,
                        lv_color_t col, bool section_hdr = false) {
        if (section_hdr) {
            lv_obj_t *sh = lv_label_create(right_card);
            lv_label_set_text(sh, lbl_txt);
            lv_obj_set_style_text_font(sh, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(sh, C_DIM, 0);
            lv_obj_set_pos(sh, 0, sy);
            sy += 20;
            lv_obj_t *d = lv_obj_create(right_card);
            lv_obj_set_size(d, RIGHT_CARD_W - PAD*2, 1);
            lv_obj_set_pos(d, 0, sy);
            lv_obj_set_style_bg_color(d, lv_color_hex(0x21262D), 0);
            lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(d, 0, 0);
            lv_obj_set_style_pad_all(d, 0, 0);
            lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            sy += 6;
            return;
        }
        const int LW = 110;
        lv_obj_t *ll = lv_label_create(right_card);
        lv_label_set_text(ll, lbl_txt);
        lv_obj_set_style_text_font(ll, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ll, C_DIM, 0);
        lv_obj_set_pos(ll, 0, sy + 2);

        lv_obj_t *vl = lv_label_create(right_card);
        lv_label_set_text(vl, "—");
        lv_obj_set_style_text_font(vl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(vl, col, 0);
        lv_obj_set_pos(vl, LW, sy);
        lv_label_set_long_mode(vl, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(vl, RIGHT_CARD_W - PAD*2 - LW);
        if (val_ptr) *val_ptr = vl;
        sy += 24;
    };

    sys_row("BATTERY",    nullptr,           C_DIM,    true);
    sys_row("Status",     &sys_bat_status,   C_GREEN);
    sys_row("Level",      &sys_bat_level,    C_COOL);
    sys_row("Voltage",    &sys_bat_voltage,  C_COOL);
    sy += 4;
    sys_row("WIFI",       nullptr,           C_DIM,    true);
    sys_row("SSID",       &sys_wifi_ssid,    C_TEXT);
    sys_row("Signal",     &sys_wifi_rssi,    C_GREEN);
    sys_row("Channel",    &sys_wifi_chan,     C_DIM);
    sys_row("IP",         &sys_wifi_ip,      C_DIM);
    sy += 4;
    sys_row("MEMORY",     nullptr,           C_DIM,    true);
    sys_row("Heap free",  &sys_heap,         C_COOL);
    sys_row("PSRAM free", &sys_psram,        C_COOL);
    sy += 4;
    sys_row("SYSTEM",     nullptr,           C_DIM,    true);
    sys_row("Chip",       &sys_chip,         C_TEXT);
    sys_row("Uptime",     &sys_uptime,       C_TEXT);
    sy += 4;
    sys_row("LAST FETCH", nullptr,           C_DIM,    true);
    sys_row("Weather",    &sys_fetch_wx,     C_DIM);
    sys_row("Space wx",   &sys_fetch_sw,     C_DIM);
    sys_row("NTP",        &sys_fetch_ntp,    C_DIM);

    make_nav_btn(scr, LV_SYMBOL_LEFT,  LV_ALIGN_LEFT_MID,  nav_left_cb);
    make_nav_btn(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, nav_right_cb);
}

// =============================================================================
//  PUBLIC: Apply brightness (live + save highlight)
// =============================================================================
void ui_apply_brightness(int b) {
    if (b < 10)  b = 10;
    if (b > 255) b = 255;
    g_setting_brightness = b;
    s_pre_dim_brightness = b;
    M5.Display.setBrightness(b);
    // Update button highlights
    for (int i = 0; i < BRIGHT_STEPS; i++) {
        if (BRIGHT_VALS[i] == b) {
            set_row_highlight(set_bright_btns, BRIGHT_STEPS, i);
            break;
        }
    }
}

// =============================================================================
//  PUBLIC: Notify touch — reset inactivity timer, wake if dimmed/off
// =============================================================================
void ui_notify_touch() {
    s_last_touch_ms = millis();
    if (s_display_off || s_display_dimmed) {
        // Restore brightness
        M5.Display.setBrightness(g_setting_brightness);
        s_display_dimmed = false;
        s_display_off    = false;
        // Optionally navigate to clock on wake
        if (s_display_off && g_setting_wake_to_clock)
            ui_show_screen(SCREEN_CLOCK);
    }
}

// =============================================================================
//  PUBLIC: Force refresh flags
// =============================================================================
void ui_request_wx_refresh() { g_force_wx_refresh = true; }
void ui_request_sw_refresh() { g_force_sw_refresh = true; }

// =============================================================================
//  PUBLIC: Settings sysinfo update — call every 5 seconds from loop()
// =============================================================================
void ui_update_settings_sysinfo() {
    if (!sys_bat_status) return;   // screen not built yet
    char buf[64];

    // ── Battery ───────────────────────────────────────────────────────────────
    bool charging  = M5.Power.isCharging();
    int  level     = M5.Power.getBatteryLevel();
    int  voltMv    = M5.Power.getBatteryVoltage();

    // Detect no-battery: Tab5 uses 7.2V 2S Li-ion (FB-NP-F550-B).
    // Valid range: 5500–8300mV (8300 not 8400, to exclude PMIC oscillation peak ~8380mV).
    bool no_battery = (voltMv < 5500 || voltMv > 8300);

    const char *bat_st;
    lv_color_t  bat_col;
    if (no_battery) { bat_st = "USB power (no battery)"; bat_col = C_DIM;    }
    else if (charging)  { bat_st = "Charging";               bat_col = C_COOL;   }
    else                { bat_st = "On battery";             bat_col = C_GREEN;  }
    lv_label_set_text(sys_bat_status, bat_st);
    lv_obj_set_style_text_color(sys_bat_status, bat_col, 0);

    if (!no_battery && level >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", level);
        lv_label_set_text(sys_bat_level, buf);
        lv_color_t lc = level > 50 ? C_GREEN : level > 20 ? C_YELLOW : C_RED;
        lv_obj_set_style_text_color(sys_bat_level, lc, 0);
    } else {
        lv_label_set_text(sys_bat_level, no_battery ? "N/A" : "—");
        lv_obj_set_style_text_color(sys_bat_level, C_DIM, 0);
    }

    if (voltMv > 0) {
        snprintf(buf, sizeof(buf), no_battery ? "%.2f V (USB)" : "%.2f V",
                 voltMv / 1000.0f);
        lv_label_set_text(sys_bat_voltage, buf);
    }

    // ── WiFi ──────────────────────────────────────────────────────────────────
    if (WiFi.status() == WL_CONNECTED) {
        lv_label_set_text(sys_wifi_ssid, WiFi.SSID().c_str());
        int rssi = WiFi.RSSI();
        const char *rssi_q = rssi > -50 ? "Excellent" :
                             rssi > -65 ? "Good"      :
                             rssi > -75 ? "Fair"      :
                             rssi > -85 ? "Weak"      : "Very weak";
        lv_color_t rc = rssi > -65 ? C_GREEN :
                        rssi > -75 ? C_YELLOW : C_RED;
        snprintf(buf, sizeof(buf), "%d dBm  (%s)", rssi, rssi_q);
        lv_label_set_text(sys_wifi_rssi, buf);
        lv_obj_set_style_text_color(sys_wifi_rssi, rc, 0);

        snprintf(buf, sizeof(buf), "Ch %d", WiFi.channel());
        lv_label_set_text(sys_wifi_chan, buf);
        lv_label_set_text(sys_wifi_ip, WiFi.localIP().toString().c_str());
    } else {
        lv_label_set_text(sys_wifi_ssid, "Disconnected");
        lv_obj_set_style_text_color(sys_wifi_ssid, C_RED, 0);
        lv_label_set_text(sys_wifi_rssi, "—");
        lv_label_set_text(sys_wifi_chan,  "—");
        lv_label_set_text(sys_wifi_ip,   "—");
    }

    // ── Memory ────────────────────────────────────────────────────────────────
    uint32_t heap_free  = ESP.getFreeHeap();
    uint32_t heap_total = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    if (heap_total == 0) heap_total = 1;  // guard div-by-zero
    snprintf(buf, sizeof(buf), "%u KB / %u KB",
             heap_free / 1024, heap_total / 1024);
    lv_label_set_text(sys_heap, buf);
    lv_color_t hc = (heap_free * 100 / heap_total) > 40 ? C_GREEN : C_YELLOW;
    lv_obj_set_style_text_color(sys_heap, hc, 0);

    uint32_t psram_free  = ESP.getFreePsram();
    uint32_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram_total > 0) {
        snprintf(buf, sizeof(buf), "%u KB / %u KB",
                 psram_free / 1024, psram_total / 1024);
    } else {
        snprintf(buf, sizeof(buf), "N/A");
    }
    lv_label_set_text(sys_psram, buf);

    // ── System ────────────────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%s  rev.%d  %u MHz",
             ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz());
    lv_label_set_text(sys_chip, buf);

    unsigned long up = millis() / 1000;
    unsigned long d = up / 86400, h = (up % 86400) / 3600,
                  m = (up % 3600) / 60,  s = up % 60;
    if (d > 0) snprintf(buf, sizeof(buf), "%lud %luh %lum", d, h, m);
    else        snprintf(buf, sizeof(buf), "%luh %lum %lus", h, m, s);
    lv_label_set_text(sys_uptime, buf);

    // ── Last fetch timestamps ──────────────────────────────────────────────────
    // These are stored in the .ino translation unit
    extern unsigned long g_t_weather_last;
    extern unsigned long g_t_space_last;
    extern unsigned long g_t_ntp_last;

    auto fmt_ago = [](unsigned long t_last, char *out, size_t n) {
        if (t_last == 0) { strncpy(out, "Never", n-1); out[n-1]='\0'; return; }
        unsigned long ago_s = (millis() - t_last) / 1000;
        if (ago_s < 60)        snprintf(out, n, "%lus ago", ago_s);
        else if (ago_s < 3600) snprintf(out, n, "%lum %lus ago", ago_s/60, ago_s%60);
        else                   snprintf(out, n, "%luh %lum ago", ago_s/3600, (ago_s%3600)/60);
    };

    // Build "HH:MM (Xm ago)" strings using local time of last fetch
    auto fmt_fetch = [&](unsigned long t_last, char *out, size_t n) {
        if (t_last == 0) { strncpy(out, "Never", n-1); out[n-1]='\0'; return; }
        time_t t = time(nullptr) - (millis() - t_last) / 1000;
        struct tm *lt = localtime(&t);
        char ago[24]; fmt_ago(t_last, ago, sizeof(ago));
        snprintf(out, n, "%02d:%02d  (%s)", lt->tm_hour, lt->tm_min, ago);
    };

    char ts[48];
    fmt_fetch(g_t_weather_last, ts, sizeof(ts));
    if (sys_fetch_wx)  lv_label_set_text(sys_fetch_wx,  ts);
    fmt_fetch(g_t_space_last,   ts, sizeof(ts));
    if (sys_fetch_sw)  lv_label_set_text(sys_fetch_sw,  ts);
    fmt_fetch(g_t_ntp_last,     ts, sizeof(ts));
    if (sys_fetch_ntp) lv_label_set_text(sys_fetch_ntp, ts);
}

// =============================================================================
//  PUBLIC: Sync settings highlights to current values (call after loading prefs)
// =============================================================================
void ui_settings_sync_highlights() {
    // Brightness
    for (int i = 0; i < BRIGHT_STEPS; i++) {
        if (BRIGHT_VALS[i] == g_setting_brightness) {
            set_row_highlight(set_bright_btns, BRIGHT_STEPS, i);
            break;
        }
    }
    // Find nearest brightness step if exact not found
    // Timeout
    for (int i = 0; i < TIMEOUT_OPTS; i++) {
        if (TIMEOUT_VALS[i] == g_setting_timeout_sec) {
            set_row_highlight(set_timeout_btns, TIMEOUT_OPTS, i);
            break;
        }
    }
    // Dim
    {
        lv_obj_t *d[2] = {set_dim_on, set_dim_off};
        set_row_highlight(d, 2, g_setting_dim_before_sleep ? 0 : 1);
    }
    // Weather interval
    for (int i = 0; i < WX_OPTS; i++) {
        if (WX_VALS[i] == g_setting_wx_interval_sec) {
            set_row_highlight(set_wx_btns, WX_OPTS, i);
            break;
        }
    }
    // Space wx interval
    for (int i = 0; i < SW_OPTS; i++) {
        if (SW_VALS[i] == g_setting_sw_interval_sec) {
            set_row_highlight(set_sw_btns, SW_OPTS, i);
            break;
        }
    }
    // Wake screen
    {
        lv_obj_t *w[2] = {set_wake_last, set_wake_clock};
        set_row_highlight(w, 2, g_setting_wake_to_clock ? 1 : 0);
    }
}

void ui_init() {
    for (int i = 0; i < SCREEN_COUNT; i++) {
        screens[i] = lv_obj_create(nullptr);
        lv_obj_set_size(screens[i], SCREEN_W, SCREEN_H);
        lv_obj_set_style_bg_color(screens[i], C_BG, 0);
        lv_obj_clear_flag(screens[i], LV_OBJ_FLAG_SCROLLABLE);

        // Attach gesture handler to every screen's background
        lv_obj_add_flag(screens[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(screens[i], gesture_cb, LV_EVENT_PRESSED,   nullptr);
        lv_obj_add_event_cb(screens[i], gesture_cb, LV_EVENT_PRESSING,  nullptr);
        lv_obj_add_event_cb(screens[i], gesture_cb, LV_EVENT_RELEASED,  nullptr);
    }

    build_clock_screen();
    build_local_screen();
    build_weather_screen();
    build_forecast_screen();
    build_astronomy_screen();
    build_solar_screen();
    build_seasons_screen();
    build_lunar_screen();
    build_planets_screen();
    build_alerts_screen();
    build_space_screen();
    build_sensor_stats_screen();
    build_tides_screen();
    build_earthquake_screen();
    build_settings_screen();

    // ── Dot indicators — drawn on every screen ────────────────────────────────
    const int total_w = SCREEN_COUNT * DOT_GAP;
    const int start_x = SCREEN_W / 2 - total_w / 2 + DOT_GAP / 2;
    for (int s = 0; s < SCREEN_COUNT; s++) {
        for (int d = 0; d < SCREEN_COUNT; d++) {
            lv_obj_t *dot = lv_obj_create(screens[s]);
            lv_obj_set_size(dot, DOT_R * 2, DOT_R * 2);
            lv_obj_set_pos(dot, start_x + d * DOT_GAP - DOT_R, DOT_Y - DOT_R);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, d == 0 ? C_TEXT : C_BORDER, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(dot, C_DIM, 0);
            lv_obj_set_style_border_width(dot, 1, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            all_dots[s][d] = dot;
        }
    }

    ui_show_screen(SCREEN_CLOCK);
}

void ui_show_screen(int id) {
    if (id < 0 || id >= SCREEN_COUNT) return;

    // Close quick-pick overlay if it exists on the departing screen
    if (qp_overlay) {
        lv_obj_del(qp_overlay);
        qp_overlay = nullptr;
    }

    cur_screen = id;

    // Instant switch — slide animation at 1280px is too expensive to render smoothly
    lv_scr_load(screens[id]);

    // Update dot row on every screen so whichever screen is active
    // shows the correct dot highlighted (all_dots[s][d] covers all screens)
    update_dots(id);
}

int ui_current_screen() { return cur_screen; }

// ── Clock update ─────────────────────────────────────────────────────────────
void ui_update_clock(const RtcDateTime &dt) {
    if (!clk_hh) return;

    char buf[8];

    // HH / MM / SS — individual labels, all font_48
    snprintf(buf, sizeof(buf), "%02d", dt.hour);
    lv_label_set_text(clk_hh, buf);

    snprintf(buf, sizeof(buf), "%02d", dt.minute);
    lv_label_set_text(clk_mm, buf);

    snprintf(buf, sizeof(buf), "%02d", dt.second);
    lv_label_set_text(clk_ss, buf);

    // Date — full weekday + date string (bright C_TEXT)
    lv_label_set_text(clk_date, rtc_date_str(dt));

    // Timezone label — DST-aware
    time_t now = time(nullptr);
    struct tm *tm_local = localtime(&now);
    lv_label_set_text(clk_tz, tm_local->tm_isdst ? "AEDT +11" : "AEST +10");
}

// ── Local sensor update (BME688) ─────────────────────────────────────────────
void ui_update_local(const SensorData &sd) {
    char buf[48];

    // ── Combined clock screen — sensor row ───────────────────────────────────
    if (clk_s_temp) {
        snprintf(buf, sizeof(buf), "%.1f \xC2\xB0""C", sd.tempC);
        lv_label_set_text(clk_s_temp, buf);
    }
    if (clk_s_hum) {
        snprintf(buf, sizeof(buf), "%.0f%%", sd.humidity);
        lv_label_set_text(clk_s_hum, buf);
    }
    if (clk_s_press) {
        const char *arrow = "";
        if (sd.pressureTrend == SensorData::TREND_RISING)  arrow = " " LV_SYMBOL_UP;
        if (sd.pressureTrend == SensorData::TREND_FALLING) arrow = " " LV_SYMBOL_DOWN;
        snprintf(buf, sizeof(buf), "%.0f hPa%s", sd.pressureHPa, arrow);
        lv_label_set_text(clk_s_press, buf);
        lv_color_t pcol = C_GREEN;
        if (sd.pressureTrend == SensorData::TREND_FALLING) pcol = C_YELLOW;
        lv_obj_set_style_text_color(clk_s_press, pcol, 0);
    }

    // ── Screen 1 — full local sensor screen ──────────────────────────────────
    if (!loc_temp) return;

    // Temperature
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", sd.tempC);
    lv_label_set_text(loc_temp, buf);
    lv_label_set_text(loc_temp_sub, "Indoor");

    // Humidity + comfort label
    snprintf(buf, sizeof(buf), "%.0f%%", sd.humidity);
    lv_label_set_text(loc_hum, buf);
    {
        const char *hlbl = sd.humidity < 30 ? "Dry" :
                           sd.humidity < 40 ? "Slightly dry" :
                           sd.humidity < 60 ? "Comfortable" :
                           sd.humidity < 70 ? "Slightly humid" : "Humid";
        lv_label_set_text(loc_hum_sub, hlbl);
    }

    // ── Pressure + trend ─────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%.1f hPa", sd.pressureHPa);
    lv_label_set_text(loc_press, buf);
    {
        lv_color_t pcol = C_TEXT;
        if (sd.pressureTrend == SensorData::TREND_RISING)  pcol = C_GREEN;
        if (sd.pressureTrend == SensorData::TREND_FALLING) pcol = C_YELLOW;
        lv_obj_set_style_text_color(loc_press, pcol, 0);
        if (loc_trend) {
            snprintf(buf, sizeof(buf), "%s  %.1f hPa/hr",
                     sensors_trend_string(sd.pressureTrend), sd.trendRateHPa_hr);
            lv_label_set_text(loc_trend, buf);
            lv_obj_set_style_text_color(loc_trend, pcol, 0);
        }
        if (wx_pressure_trend) {
            lv_label_set_text(wx_pressure_trend, sensors_trend_string(sd.pressureTrend));
            lv_obj_set_style_text_color(wx_pressure_trend, pcol, 0);
        }
    }

    // ── IAQ panel (right column) ──────────────────────────────────────────────
    {
        lv_color_t iq_col = C_GREEN;
        if      (sd.iaqScore > IAQ_SEVERELY_POLLUTED)   iq_col = C_RED;
        else if (sd.iaqScore > IAQ_HEAVILY_POLLUTED)    iq_col = lv_color_hex(0xA050D0);
        else if (sd.iaqScore > IAQ_MODERATELY_POLLUTED) iq_col = C_WARM;
        else if (sd.iaqScore > IAQ_LIGHTLY_POLLUTED)    iq_col = C_YELLOW;
        else if (sd.iaqScore > IAQ_GOOD)                iq_col = C_ACCENT;

        bool warming = (sd.iaqAccuracy == 0 && !sd.gasValid);

        if (loc_iaq) {
            if (warming) {
                lv_label_set_text(loc_iaq, "--");
                lv_obj_set_style_text_color(loc_iaq, C_BORDER, 0);
            } else {
                snprintf(buf, sizeof(buf), "%.0f", sd.iaqScore);
                lv_label_set_text(loc_iaq, buf);
                lv_obj_set_style_text_color(loc_iaq, iq_col, 0);
            }
        }
        if (loc_iaq_label) {
            const char *qlbl = warming ? "Warming up"
                                       : sensors_iaq_label(sd.iaqScore, sd.iaqAccuracy);
            lv_label_set_text(loc_iaq_label, qlbl);
            lv_obj_set_style_text_color(loc_iaq_label, warming ? C_BORDER : iq_col, 0);
        }
        if (loc_iaq_acc_badge) {
            lv_label_set_text(loc_iaq_acc_badge, sensors_accuracy_label(sd.iaqAccuracy));
            lv_color_t badge_bg = C_DIM;
            if      (sd.iaqAccuracy == 3) badge_bg = C_GREEN;
            else if (sd.iaqAccuracy == 2) badge_bg = C_ACCENT;
            else if (sd.iaqAccuracy == 1) badge_bg = C_YELLOW;
            lv_obj_set_style_bg_color(loc_iaq_acc_badge, badge_bg, 0);
            lv_obj_set_style_text_color(loc_iaq_acc_badge,
                sd.iaqAccuracy == 0 ? C_DIM : C_BG, 0);
        }
        if (loc_iaq_bar_fill) {
            const int BAR_W = (SCREEN_W - (NAV_BTN_W + PAD + 230 + 12) - NAV_BTN_W - PAD) - PAD * 2;
            int fill_w = (int)((sd.iaqScore / 500.0f) * BAR_W);
            if (fill_w < 0) fill_w = 0;
            if (fill_w > BAR_W) fill_w = BAR_W;
            lv_obj_set_width(loc_iaq_bar_fill, fill_w);
            lv_obj_set_style_bg_color(loc_iaq_bar_fill, iq_col, 0);
        }
        if (loc_iaq_resist) {
            if (sd.gasResistanceOhm > 0) {
                int rk = (int)(sd.gasResistanceOhm / 1000);
                int rr = (int)sd.gasResistanceOhm % 1000;
                if (rk > 0) snprintf(buf, sizeof(buf), "Gas resistance: %d,%03d ohm", rk, rr);
                else        snprintf(buf, sizeof(buf), "Gas resistance: %d ohm", (int)sd.gasResistanceOhm);
            } else {
                snprintf(buf, sizeof(buf), "Gas resistance: --");
            }
            lv_label_set_text(loc_iaq_resist, buf);
        }
        if (loc_iaq_base) {
            float baseline = sensors_get_baseline();
            int bk = (int)(baseline / 1000);
            int br = (int)baseline % 1000;
            if (bk > 0) snprintf(buf, sizeof(buf), "Baseline: %d,%03d ohm", bk, br);
            else        snprintf(buf, sizeof(buf), "Baseline: %d ohm", (int)baseline);
            lv_label_set_text(loc_iaq_base, buf);
        }
    }

    // ── Pressure trend chart ──────────────────────────────────────────────────
    if (loc_chart_canvas && loc_chart_buf) {
        // Remove any LVGL label children from previous update (Y/X axis labels),
        // then re-fill the canvas background before redrawing everything.
        lv_obj_clean(loc_chart_canvas);
        lv_canvas_set_buffer(loc_chart_canvas, loc_chart_buf,
                             LOC_CHART_W, LOC_CHART_H, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(loc_chart_canvas, C_CARD, LV_OPA_COVER);
        const int W = LOC_CHART_W, H = LOC_CHART_H;
        // PAD_L wide enough for "1020" (4 chars) at font_14 (~28px) + tick + gap
        const int PAD_L = 48, PAD_R = 12, PAD_T = 14, PAD_B = 28;
        const int CW = W - PAD_L - PAD_R;
        const int CH = H - PAD_T - PAD_B;

        // X/Y axis rails always drawn
        canvas_line(loc_chart_canvas, PAD_L, H - PAD_B, W - PAD_R, H - PAD_B, C_BORDER, 1);
        canvas_line(loc_chart_canvas, PAD_L, PAD_T,     PAD_L,     H - PAD_B, C_BORDER, 1);

        if (sd.historyCount < 2) {
            // Not enough data yet — show wait label, draw single alive-dot if 1 reading
            if (loc_wait_lbl) lv_obj_clear_flag(loc_wait_lbl, LV_OBJ_FLAG_HIDDEN);
            if (sd.historyCount == 1) {
                int dotX = PAD_L + CW / 2;
                int dotY = PAD_T + CH / 2;
                canvas_fill_circle(loc_chart_canvas, dotX, dotY, 5, C_ACCENT);
                canvas_line(loc_chart_canvas, dotX, H - PAD_B, dotX, H - PAD_B + 4, C_DIM, 1);
            }
        } else {
            // Data ready — hide the "Collecting data" overlay
            if (loc_wait_lbl) lv_obj_add_flag(loc_wait_lbl, LV_OBJ_FLAG_HIDDEN);

            // Y scale: round to nearest 0.5 hPa, add 1 hPa padding each side
            float pmin = sd.pressureHistory[0], pmax = sd.pressureHistory[0];
            for (int i = 1; i < sd.historyCount; i++) {
                if (sd.pressureHistory[i] < pmin) pmin = sd.pressureHistory[i];
                if (sd.pressureHistory[i] > pmax) pmax = sd.pressureHistory[i];
            }
            pmin = floorf((pmin - 1.0f) * 2.0f) / 2.0f;  // round down to 0.5
            pmax = ceilf ((pmax + 1.0f) * 2.0f) / 2.0f;  // round up   to 0.5
            float prange = pmax - pmin;
            if (prange < 0.5f) { pmin -= 0.5f; pmax += 0.5f; prange = pmax - pmin; }

            // ── 4 horizontal grid lines with Y-axis hPa labels ────────────────
            // Lines at top, 1/3, 2/3, bottom of chart area
            for (int gi = 0; gi <= 3; gi++) {
                int gy = PAD_T + (int)(CH * gi / 3.0f);
                // Grid line (dim for inner lines, border-colour for the baseline)
                lv_color_t gc = (gi == 3) ? C_BORDER : lv_color_hex(0x21262D);
                canvas_line(loc_chart_canvas, PAD_L, gy, W - PAD_R, gy, gc, 1);

                // Tick on Y axis
                canvas_line(loc_chart_canvas, PAD_L - 4, gy, PAD_L, gy, C_DIM, 1);

                // Y-axis label — hPa value at this grid line
                // gi=0 → pmax, gi=3 → pmin
                float labelVal = pmax - (float)gi / 3.0f * prange;
                char ylbl[12];
                // Show one decimal place if range is small, otherwise integer
                if (prange <= 4.0f)
                    snprintf(ylbl, sizeof(ylbl), "%.1f", labelVal);
                else
                    snprintf(ylbl, sizeof(ylbl), "%.0f", labelVal);

                // Draw label as LVGL pixel text is not available on canvas in LVGL8.
                // Use canvas_line segments to approximate — but that is impractical.
                // Instead we place a temporary LVGL label at the correct screen
                // position once per update; we store nothing — rebuild each call.
                // The label is parented to the canvas so it moves with it.
                lv_obj_t *yl = lv_label_create(loc_chart_canvas);
                lv_label_set_text(yl, ylbl);
                lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(yl, C_DIM, 0);
                lv_obj_set_style_bg_opa(yl, LV_OPA_TRANSP, 0);
                // Right-align the label just left of the Y axis tick
                lv_obj_set_style_text_align(yl, LV_TEXT_ALIGN_RIGHT, 0);
                lv_obj_set_size(yl, PAD_L - 6, 16);
                lv_obj_set_pos(yl, 0, gy - 8);
            }

            // Line colour by trend
            lv_color_t lineCol = C_ACCENT;
            if (sd.pressureTrend == SensorData::TREND_RISING)  lineCol = C_GREEN;
            if (sd.pressureTrend == SensorData::TREND_FALLING) lineCol = C_YELLOW;

            // Plot line
            int prevX = -1, prevY = -1;
            for (int i = 0; i < sd.historyCount; i++) {
                int px = PAD_L + (int)((float)i / (float)(sd.historyCount - 1) * CW);
                int py = PAD_T + (int)((pmax - sd.pressureHistory[i]) / prange * CH);
                if (prevX >= 0)
                    canvas_line(loc_chart_canvas, prevX, prevY, px, py, lineCol, 3);
                prevX = px;  prevY = py;
            }
            // Current value dot
            if (prevX >= 0)
                canvas_fill_circle(loc_chart_canvas, prevX, prevY, 6, lineCol);

            // ── X-axis ticks + time labels every ~3 readings ──────────────────
            // Each reading = 5 min; label every 3rd = every 15 min
            // Time labels show minutes-ago: "60m", "45m", "30m", "15m", "now"
            int totalReadings = sd.historyCount;
            for (int i = 0; i < totalReadings; i++) {
                int px = PAD_L + (int)((float)i / (float)(totalReadings - 1) * CW);
                canvas_line(loc_chart_canvas, px, H - PAD_B, px, H - PAD_B + 4, C_BORDER, 1);

                // Label every 3rd tick, plus always label the last (current) point
                bool labelThis = (i == totalReadings - 1) ||
                                 ((totalReadings - 1 - i) % 3 == 0);
                if (labelThis) {
                    int minsAgo = (totalReadings - 1 - i) * 5;
                    char xlbl[8];
                    if (minsAgo == 0)       snprintf(xlbl, sizeof(xlbl), "now");
                    else                    snprintf(xlbl, sizeof(xlbl), "%dm", minsAgo);

                    lv_obj_t *xl = lv_label_create(loc_chart_canvas);
                    lv_label_set_text(xl, xlbl);
                    lv_obj_set_style_text_font(xl, &lv_font_montserrat_14, 0);
                    lv_obj_set_style_text_color(xl, C_DIM, 0);
                    lv_obj_set_style_bg_opa(xl, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_text_align(xl, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_size(xl, 36, 16);
                    lv_obj_set_pos(xl, px - 18, H - PAD_B + 6);
                }
            }
        }
    }

    // Update statusbar sensor values on all screens
    ui_update_statusbar_sensors(sd);

    // ── Comfort panel ─────────────────────────────────────────────────────────
    if (loc_dewpoint) {
        // Dew point — Magnus approximation
        float t = sd.tempC, rh = sd.humidity;
        float alpha = (17.625f * t) / (243.04f + t) + logf(rh / 100.0f);
        float dp = (243.04f * alpha) / (17.625f - alpha);
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", dp);
        lv_label_set_text(loc_dewpoint, buf);
        // Bar 0-30°C range
        int dp_pct = (int)((dp + 5.0f) / 35.0f * 100.0f);
        if (dp_pct < 0) dp_pct = 0; if (dp_pct > 100) dp_pct = 100;

        // Heat index — Steadman simplified (valid above 27°C / 40% RH)
        float hi_val = t;
        if (t >= 27.0f && rh >= 40.0f) {
            hi_val = -8.78469475556f
                   + 1.61139411f * t
                   + 2.33854883889f * rh
                   - 0.14611605f * t * rh
                   - 0.012308094f * t * t
                   - 0.016424828f * rh * rh
                   + 0.002211732f * t * t * rh
                   + 0.00072546f * t * rh * rh
                   - 0.000003582f * t * t * rh * rh;
        }
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", hi_val);
        lv_label_set_text(loc_heatidx, buf);
        int hi_pct = (int)((hi_val - 20.0f) / 25.0f * 100.0f);
        if (hi_pct < 0) hi_pct = 0; if (hi_pct > 100) hi_pct = 100;

        // Comfort score — based on temp + humidity ranges
        // Ideal: 20-24°C, 40-60% RH
        float t_score  = 1.0f - fminf(fabsf(t  - 22.0f) / 10.0f, 1.0f);
        float rh_score = 1.0f - fminf(fabsf(rh - 50.0f) / 30.0f, 1.0f);
        float comfort  = (t_score + rh_score) / 2.0f * 100.0f;
        int comf_pct   = (int)comfort;

        const char *comf_lbl = comf_pct >= 80 ? "Excellent" :
                               comf_pct >= 60 ? "Good" :
                               comf_pct >= 40 ? "Fair" :
                               comf_pct >= 20 ? "Poor" : "Uncomfortable";
        lv_color_t comf_col  = comf_pct >= 60 ? C_GREEN :
                               comf_pct >= 40 ? C_YELLOW : C_RED;
        lv_label_set_text(loc_comfort, comf_lbl);
        lv_obj_set_style_text_color(loc_comfort, comf_col, 0);
        lv_obj_set_style_bg_color(loc_comfbar, comf_col, 0);

        // Update bar fill widths — get parent card width
        if (loc_dewbar && loc_heatbar && loc_comfbar) {
            lv_obj_t *card = lv_obj_get_parent(loc_dewbar);
            int card_inner = lv_obj_get_width(card) - PAD * 2;
            int col_w = card_inner / 3 - 8;
            lv_obj_set_width(loc_dewbar,  col_w * dp_pct  / 100);
            lv_obj_set_width(loc_heatbar, col_w * hi_pct  / 100);
            lv_obj_set_width(loc_comfbar, col_w * comf_pct / 100);
        }
    }
}

// ── Hourly sparkline draw ─────────────────────────────────────────────────────
// Draws a bezier temperature sparkline on wx_spark_canvas using canvas_line().
// Points are centred over each hourly slot card (same x-grid as the cards).
// Called from ui_update_weather() after hourly slot data is populated.
static void draw_hourly_sparkline(const WeatherResult &wr) {
    if (!wx_spark_canvas || !wx_spark_buf || wx_spark_w <= 0) return;
    if (wr.hourlyCount < 2) return;

    const int W = wx_spark_w;
    const int H = WX_SPARK_H;

    // Clear canvas
    lv_canvas_set_buffer(wx_spark_canvas, wx_spark_buf, W, H, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(wx_spark_canvas, lv_color_hex(0x0D1117), LV_OPA_COVER);

    // Slot geometry — must match build constants
    const int TOTAL_W = W;   // canvas == LEFT_W - PAD*2
    const int GAP     = 12;
    const int SLOT_W  = (TOTAL_W - 5 * GAP) / WX_HOURLY_SLOTS;

    // X centre of each slot (relative to canvas, which starts at hero pad offset)
    int cx[WX_HOURLY_SLOTS];
    for (int i = 0; i < WX_HOURLY_SLOTS; i++) {
        cx[i] = i * (SLOT_W + GAP) + SLOT_W / 2;
    }

    // Gather valid temps and find range
    int count = (wr.hourlyCount < WX_HOURLY_SLOTS) ? wr.hourlyCount : WX_HOURLY_SLOTS;
    float tmin = wr.hourly[0].tempC, tmax = wr.hourly[0].tempC;
    for (int i = 1; i < count; i++) {
        if (wr.hourly[i].tempC < tmin) tmin = wr.hourly[i].tempC;
        if (wr.hourly[i].tempC > tmax) tmax = wr.hourly[i].tempC;
    }
    float trange = tmax - tmin;
    if (trange < 1.0f) trange = 1.0f;   // avoid div-by-zero on flat data

    // Map temps to Y — top margin 10px (for label), bottom margin 4px
    const int Y_TOP = 10, Y_BOT = H - 4;
    auto tempY = [&](float t) -> int {
        return Y_BOT - (int)((t - tmin) / trange * (Y_BOT - Y_TOP));
    };

    int px[WX_HOURLY_SLOTS], py[WX_HOURLY_SLOTS];
    for (int i = 0; i < count; i++) {
        px[i] = cx[i];
        py[i] = tempY(wr.hourly[i].tempC);
    }

    // Draw line segments (straight segments — canvas has no bezier, but at 6 pts it reads fine)
    lv_color_t line_col = lv_color_hex(0x58A6FF);   // C_ACCENT
    lv_color_t fill_col = lv_color_hex(0x0D2030);   // very dark blue fill under line

    // Fill under the line: for each x column fill a vertical strip from line to bottom
    for (int i = 0; i < count - 1; i++) {
        int x0 = px[i], y0 = py[i];
        int x1 = px[i+1], y1 = py[i+1];
        int dx = x1 - x0;
        if (dx <= 0) continue;
        for (int x = x0; x <= x1; x++) {
            // Interpolate y along this segment
            int seg_y = y0 + (y1 - y0) * (x - x0) / dx;
            // Fill from seg_y+1 down to H-1
            for (int y = seg_y + 1; y < H; y++) {
                canvas_line(wx_spark_canvas, x, y, x, y, fill_col);
            }
        }
    }

    // Draw the connecting lines (3px thick for visibility)
    for (int i = 0; i < count - 1; i++) {
        int x0 = px[i], y0 = py[i];
        int x1 = px[i+1], y1 = py[i+1];
        canvas_line(wx_spark_canvas, x0, y0,   x1, y1,   line_col);
        canvas_line(wx_spark_canvas, x0, y0-1, x1, y1-1, line_col);
        canvas_line(wx_spark_canvas, x0, y0+1, x1, y1+1, line_col);
    }

    // Dot at each point (5px filled circle)
    for (int i = 0; i < count; i++) {
        canvas_fill_circle(wx_spark_canvas, px[i], py[i], 3, line_col);
    }

    lv_obj_invalidate(wx_spark_canvas);
}

// ── Weather API update ────────────────────────────────────────────────────────
void ui_update_weather(const WeatherResult &wr) {
    char buf[64];

    // ── Combined clock screen — weather panel ─────────────────────────────────
    if (clk_wx_temp) {
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", wr.tempC);
        lv_label_set_text(clk_wx_temp, buf);

        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", wr.feelsLikeC);
        lv_label_set_text(clk_wx_feels, buf);

        lv_label_set_text(clk_wx_cond, wr.condition);

        // Build extended description lines from available fields.
        // Line 1: wind direction and speed with humidity
        snprintf(buf, sizeof(buf), "Wind %.0f km/h %s  |  Humidity %d%%",
                 wr.windKph, wr.windDir, wr.humidity);
        lv_label_set_text(clk_wx_desc1, buf);

        // Line 2: UV + visibility + precipitation
        const char *uv_desc = wr.uvIndex < 3 ? "Low" :
                              wr.uvIndex < 6 ? "Moderate" :
                              wr.uvIndex < 8 ? "High" :
                              wr.uvIndex < 11 ? "Very High" : "Extreme";
        snprintf(buf, sizeof(buf), "UV %.1f (%s)  |  Vis %.0f km  |  Rain %.1f mm",
                 wr.uvIndex, uv_desc, wr.visKm, wr.precipMM);
        lv_label_set_text(clk_wx_desc2, buf);

        // Detail grid
        snprintf(buf, sizeof(buf), "%.0f km/h %s", wr.windKph, wr.windDir);
        lv_label_set_text(clk_wx_wind, buf);

        snprintf(buf, sizeof(buf), "%d%%", wr.humidity);
        lv_label_set_text(clk_wx_hum, buf);

        snprintf(buf, sizeof(buf), "%.1f  %s", wr.uvIndex, uv_desc);
        lv_label_set_text(clk_wx_uv, buf);
        lv_color_t uvcol = C_GREEN;
        if (wr.uvIndex >= 3)  uvcol = C_YELLOW;
        if (wr.uvIndex >= 8)  uvcol = C_RED;
        lv_obj_set_style_text_color(clk_wx_uv, uvcol, 0);

        snprintf(buf, sizeof(buf), "%.0f km", wr.visKm);
        lv_label_set_text(clk_wx_vis, buf);

        snprintf(buf, sizeof(buf), "%.1f mm", wr.precipMM);
        lv_label_set_text(clk_wx_rain, buf);

        snprintf(buf, sizeof(buf), "%s\n%s", wr.sunrise, wr.sunset);
        lv_label_set_text(clk_wx_sun, buf);

        // Forecast paragraph — tomorrow + day after
        if (clk_wx_forecast) {
            char fc[128];
            const ForecastDay &t1 = wr.forecast[1];  // tomorrow
            const ForecastDay &t2 = wr.forecast[2];  // day after
            snprintf(fc, sizeof(fc),
                     "%s: %s, %.0f-%.0f\xC2\xB0""C, %d%% rain.  "
                     "%s: %s, %.0f-%.0f\xC2\xB0""C, %d%% rain.",
                     t1.dayName, t1.condition,
                     t1.minTempC, t1.maxTempC, t1.rainChancePct,
                     t2.dayName, t2.condition,
                     t2.minTempC, t2.maxTempC, t2.rainChancePct);
            lv_label_set_text(clk_wx_forecast, fc);
        }

        // ── Outlook strip — 5 slots ───────────────────────────────────────────
        // Slot 0: Today (live icon + current temp, hi/lo from forecast[0])
        // Slot 1: Tomorrow = forecast[1]
        // Slot 2: Day after = forecast[2]
        // Slot 3: blank (no data on 3-day plan)
        // Slot 4: Moon phase
        if (out_canvas[0]) {
            char tmp[32];

            // Slot 0 — Today
            draw_wx_icon(out_canvas[0], wx_icon_category(wr.conditionCode, wr.isDay));
            lv_label_set_text(out_day[0], "Today");
            snprintf(tmp, sizeof(tmp), "%.0f/%.0f\xC2\xB0",
                     wr.forecast[0].maxTempC, wr.forecast[0].minTempC);
            lv_label_set_text(out_temp[0], tmp);
            if (wr.forecast[0].rainChancePct > 0) {
                snprintf(tmp, sizeof(tmp), "%d%%", wr.forecast[0].rainChancePct);
                lv_label_set_text(out_rain[0], tmp);
            } else {
                lv_label_set_text(out_rain[0], "");
            }

            // Slots 1-2 — tomorrow and day after (forecast[1] and forecast[2])
            for (int i = 1; i <= 2; i++) {
                const ForecastDay &f = wr.forecast[i];
                draw_wx_icon(out_canvas[i], wx_icon_category(f.conditionCode));
                char dname[4] = {f.dayName[0], f.dayName[1], f.dayName[2], 0};
                lv_label_set_text(out_day[i], dname);
                snprintf(tmp, sizeof(tmp), "%.0f/%.0f\xC2\xB0",
                         f.maxTempC, f.minTempC);
                lv_label_set_text(out_temp[i], tmp);
                if (f.rainChancePct > 0) {
                    snprintf(tmp, sizeof(tmp), "%d%%", f.rainChancePct);
                    lv_label_set_text(out_rain[i], tmp);
                } else {
                    lv_label_set_text(out_rain[i], "");
                }
            }

            // Slot 3 — blank
            lv_canvas_fill_bg(out_canvas[3], C_BG, LV_OPA_COVER);
            lv_label_set_text(out_day[3],  "");
            lv_label_set_text(out_temp[3], "");
            lv_label_set_text(out_rain[3], "");

            // Slot 4 — Moon phase
            // Reuse lunar_age_now() which is defined earlier in this file
            float age = lunar_age_now();
            float angle = (age / 29.53f) * 360.0f;
            // Draw moon disc on icon canvas
            lv_canvas_fill_bg(out_canvas[4], C_BG, LV_OPA_COVER);
            draw_moon_disc(out_canvas[4],
                           OUTLOOK_ICON_W/2, OUTLOOK_ICON_H/2 - 2, 28,
                           age,
                           lv_color_hex(0xCDD9E5), C_BG,
                           lv_color_hex(0x1A1E24));
            canvas_stroke_circle(out_canvas[4],
                                 OUTLOOK_ICON_W/2, OUTLOOK_ICON_H/2 - 2, 28,
                                 C_DIM, 1);
            lv_label_set_text(out_day[4], wr.moonPhase[0] ? wr.moonPhase : "Moon");
            int illum = (int)((1.0f - cosf(angle * (float)M_PI / 180.0f)) / 2.0f * 100.0f);
            snprintf(tmp, sizeof(tmp), "%d%% lit", illum);
            lv_label_set_text(out_temp[4], tmp);
            lv_label_set_text(out_rain[4], "");
        }

        time_t now = time(nullptr);
        struct tm *t = localtime(&now);
        snprintf(buf, sizeof(buf), "Updated %02d:%02d", t->tm_hour, t->tm_min);
        lv_label_set_text(clk_wx_updated, buf);
    }

    // ── Screen 2 — standalone weather screen ─────────────────────────────────
    if (!wx_temp) return;

    // ── Hero — temp / feels / humidity / condition ────────────────────────────
    snprintf(buf, sizeof(buf), "%.1f \xC2\xB0""C", wr.tempC);
    lv_label_set_text(wx_temp, buf);

    snprintf(buf, sizeof(buf), "Feels like %.1f\xC2\xB0", wr.feelsLikeC);
    lv_label_set_text(wx_feels, buf);

    snprintf(buf, sizeof(buf), "%d%%", wr.humidity);
    lv_label_set_text(wx_hum_hero, buf);

    lv_label_set_text(wx_cond, wr.condition);

    // Hero icon
    if (wx_icon_canvas) {
        lv_canvas_fill_bg(wx_icon_canvas, C_CARD, LV_OPA_COVER);
        draw_wx_icon(wx_icon_canvas, wx_icon_category(wr.conditionCode, wr.isDay));
    }

    // ── Hourly strip ─────────────────────────────────────────────────────────
    for (int i = 0; i < WX_HOURLY_SLOTS && i < wr.hourlyCount; i++) {
        const HourlySlot &hs = wr.hourly[i];

        // Time label — "Now" for slot 0, "H PM/AM" for rest
        if (i == 0) {
            lv_label_set_text(wx_h_time[i], "Now");
            lv_obj_set_style_text_color(wx_h_time[i], C_ACCENT, 0);
            lv_obj_set_style_border_color(wx_h_card[i], C_ACCENT, 0);
        } else {
            int h12 = hs.hour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(buf, sizeof(buf), "%d %s", h12, hs.hour < 12 ? "AM" : "PM");
            lv_label_set_text(wx_h_time[i], buf);
            lv_obj_set_style_text_color(wx_h_time[i], C_DIM, 0);
            lv_obj_set_style_border_color(wx_h_card[i], C_BORDER, 0);
        }

        // Icon — respect isDay so night slots don't show a sun
        if (wx_h_canvas[i]) {
            lv_canvas_fill_bg(wx_h_canvas[i], lv_color_hex(0x0D1117), LV_OPA_COVER);
            draw_wx_icon(wx_h_canvas[i], wx_icon_category(hs.conditionCode, hs.isDay));
        }

        // Temp
        snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", hs.tempC);
        lv_label_set_text(wx_h_temp[i], buf);

        // Rain %
        if (hs.rainChancePct > 0) {
            snprintf(buf, sizeof(buf), "%d%%", hs.rainChancePct);
            lv_label_set_text(wx_h_rain[i], buf);
            lv_color_t rc = hs.rainChancePct >= 60 ? C_ACCENT : C_COOL;
            lv_obj_set_style_text_color(wx_h_rain[i], rc, 0);
        } else {
            lv_label_set_text(wx_h_rain[i], "");
            lv_obj_set_style_text_color(wx_h_rain[i], C_BORDER, 0);
        }
    }

    // Sparkline — drawn after all slots are populated so temps are available
    draw_hourly_sparkline(wr);

    // ── Mini stats grid ───────────────────────────────────────────────────────
    snprintf(buf, sizeof(buf), "%.0f km/h %s", wr.windKph, wr.windDir);
    lv_label_set_text(wx_wind, buf);
    // Gust — now parsed from API
    snprintf(buf, sizeof(buf), "Gust %.0f km/h", wr.windGustKph);
    lv_label_set_text(wx_wind_gust, buf);

    snprintf(buf, sizeof(buf), "%.0f km", wr.visKm);
    lv_label_set_text(wx_vis, buf);

    snprintf(buf, sizeof(buf), "%.1f mm", wr.precipMM);
    lv_label_set_text(wx_precip, buf);

    snprintf(buf, sizeof(buf), "%d%% rain chance", wr.forecast[0].rainChancePct);
    lv_label_set_text(wx_hum, buf);

    // Pressure — now parsed from API (pressure_mb = hPa)
    snprintf(buf, sizeof(buf), "%.0f hPa", wr.pressureMb);
    lv_label_set_text(wx_pressure, buf);
    // Trend from BME688 sensor if available, otherwise blank
    lv_label_set_text(wx_pressure_trend, "");

    // Dew point — Magnus formula (same as Screen 1, accurate to ±0.35°C)
    {
        float t2 = wr.tempC, rh = (float)wr.humidity;
        float alpha = (17.625f * t2) / (243.04f + t2) + logf(rh / 100.0f);
        float dp    = (243.04f * alpha) / (17.625f - alpha);
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0""C", dp);
        lv_label_set_text(wx_dewpoint, buf);
    }

    // Cloud cover
    if (wx_cloud) {
        snprintf(buf, sizeof(buf), "%d%%", wr.cloudPct);
        lv_label_set_text(wx_cloud, buf);
    }

    // ── UV card ───────────────────────────────────────────────────────────────
    if (wx_uv_num) {
        snprintf(buf, sizeof(buf), "%.1f", wr.uvIndex);
        lv_label_set_text(wx_uv_num, buf);

        const char *uv_cat   = wr.uvIndex < 3  ? "Low" :
                               wr.uvIndex < 6  ? "Moderate" :
                               wr.uvIndex < 8  ? "High" :
                               wr.uvIndex < 11 ? "Very High" : "Extreme";
        lv_label_set_text(wx_uv_cat, uv_cat);

        lv_color_t uvcol = C_GREEN;
        if (wr.uvIndex >= 3)  uvcol = C_YELLOW;
        if (wr.uvIndex >= 8)  uvcol = C_RED;
        lv_obj_set_style_text_color(wx_uv_num, uvcol, 0);
        lv_obj_set_style_text_color(wx_uv_cat, uvcol, 0);

        lv_label_set_text(wx_uv_peak, "Peak: see forecast");

        // UV bar advice
        const char *advice = wr.uvIndex < 3  ? "No protection needed" :
                             wr.uvIndex < 6  ? "SPF 30+ recommended" :
                             wr.uvIndex < 8  ? "SPF 50+ - seek shade midday" :
                             wr.uvIndex < 11 ? "SPF 50+ - minimise sun exposure" :
                                               "Stay indoors 10am-4pm";
        lv_label_set_text(wx_uv_advice, advice);
        lv_obj_set_style_text_color(wx_uv_advice, uvcol, 0);

        // Move UV marker along bar
        if (wx_uv_marker && wx_uv_bar_w > 0) {
            float pct = wr.uvIndex / 11.0f;
            if (pct > 1.0f) pct = 1.0f;
            int mx = (int)(pct * (float)wx_uv_bar_w);
            lv_obj_set_x(wx_uv_marker, mx - 7);  // centre the 14px dot on bar
        }
    }

    // ── Air Quality (UV card lower half) ─────────────────────────────────────
    if (wr.aqiValid) {
        static const char *aqi_cats[] = {
            "", "Good", "Moderate", "Sens. Groups", "Unhealthy", "Very Unhealthy", "Hazardous"
        };
        static const lv_color_t aqi_cols[] = {
            C_DIM,
            lv_color_hex(0x3FB950),  // 1 Good
            lv_color_hex(0xD29922),  // 2 Moderate
            lv_color_hex(0xFF7B50),  // 3 Sensitive groups
            lv_color_hex(0xF85149),  // 4 Unhealthy
            lv_color_hex(0x9B59B6),  // 5 Very Unhealthy
            lv_color_hex(0x8B0000),  // 6 Hazardous
        };
        int epa = wr.aqiUsEpa;
        if (epa < 1) epa = 1;
        if (epa > 6) epa = 6;
        lv_color_t ac = aqi_cols[epa];
        if (wx_aqi_cat) {
            lv_label_set_text(wx_aqi_cat, aqi_cats[epa]);
            lv_obj_set_style_text_color(wx_aqi_cat, ac, 0);
        }
        if (wx_aqi_epa) {
            snprintf(buf, sizeof(buf), "EPA %d/6", epa);
            lv_label_set_text(wx_aqi_epa, buf);
            lv_obj_set_style_text_color(wx_aqi_epa, ac, 0);
        }
        if (wx_aqi_pm25) {
            snprintf(buf, sizeof(buf), "%.1f", wr.aqiPm25);
            lv_label_set_text(wx_aqi_pm25, buf);
        }
        if (wx_aqi_pm10) {
            snprintf(buf, sizeof(buf), "%.1f", wr.aqiPm10);
            lv_label_set_text(wx_aqi_pm10, buf);
        }
        if (wx_aqi_o3) {
            snprintf(buf, sizeof(buf), "%.1f", wr.aqiO3);
            lv_label_set_text(wx_aqi_o3, buf);
        }
        if (wx_aqi_no2) {
            snprintf(buf, sizeof(buf), "%.2f", wr.aqiNo2);
            lv_label_set_text(wx_aqi_no2, buf);
        }
    } else {
        if (wx_aqi_cat) { lv_label_set_text(wx_aqi_cat, "No data"); }
        if (wx_aqi_epa) { lv_label_set_text(wx_aqi_epa, ""); }
    }

    // ── Bottom-right detail card ──────────────────────────────────────────────
    if (wx_sunrise) {
        lv_label_set_text(wx_sunrise, wr.sunrise);
        lv_label_set_text(wx_sunset,  wr.sunset);

        // Solar noon from sunrise + half day length
        int srMins  = parse_time_to_mins(wr.sunrise);
        int ssMins  = parse_time_to_mins(wr.sunset);
        int noonMin = (srMins + ssMins) / 2;
        lv_label_set_text(wx_solarnoon, mins_to_time_str(noonMin));

        // Countdowns using current time
        {
            time_t now2 = time(nullptr);
            struct tm *lt = localtime(&now2);
            int curMin = lt->tm_hour * 60 + lt->tm_min;

            int srDiff = srMins - curMin;
            int ssDiff = ssMins - curMin;
            int nnDiff = noonMin - curMin;

            auto fmt_diff = [](int d, char *out, size_t n) {
                if (d < 0) {
                    int ad = -d;
                    if (ad < 60) snprintf(out, n, "%dm ago", ad);
                    else         snprintf(out, n, "%dh %dm ago", ad/60, ad%60);
                } else if (d < 60) {
                    snprintf(out, n, "in %dm", d);
                } else {
                    snprintf(out, n, "in %dh %dm", d/60, d%60);
                }
            };
            char sub[32];
            fmt_diff(srDiff, sub, sizeof(sub)); lv_label_set_text(wx_sunrise_sub, sub);
            fmt_diff(nnDiff, sub, sizeof(sub)); lv_label_set_text(wx_solarnoon_sub, sub);
            fmt_diff(ssDiff, sub, sizeof(sub));
            // append day length
            int dayLen = ssMins - srMins;
            char ssfull[48];
            snprintf(ssfull, sizeof(ssfull), "%s  |  %dh %dm day",
                     sub, dayLen/60, dayLen%60);
            lv_label_set_text(wx_sunset_sub, ssfull);
        }

        // Moon
        lv_label_set_text(wx_moon_phase, wr.moonPhase);
        snprintf(buf, sizeof(buf), "%d%% illuminated", wr.moonIllumPct);
        lv_label_set_text(wx_moon_sub, buf);

        // Next equinox/solstice — reuse season calculation
        {
            time_t now3 = time(nullptr);
            struct tm *lt = localtime(&now3);
            int year = lt->tm_year + 1900;
            SeasonEvts e = calc_season_events(year);
            int doy = day_of_year(lt->tm_mday, lt->tm_mon + 1, year);

            int dMar = ((e.mar-doy)%365+365)%365; if(!dMar) dMar=365;
            int dJun = ((e.jun-doy)%365+365)%365; if(!dJun) dJun=365;
            int dSep = ((e.sep-doy)%365+365)%365; if(!dSep) dSep=365;
            int dDec = ((e.dec-doy)%365+365)%365; if(!dDec) dDec=365;

            int minD = dMar;
            if (dJun < minD) minD = dJun;
            if (dSep < minD) minD = dSep;
            if (dDec < minD) minD = dDec;

            const char *evtName = (dMar==minD) ? "Mar Equinox" :
                                  (dJun==minD) ? "Jun Solstice" :
                                  (dSep==minD) ? "Sep Equinox"  : "Dec Solstice";
            int nextDoy = (dMar==minD)?e.mar:(dJun==minD)?e.jun:(dSep==minD)?e.sep:e.dec;

            lv_label_set_text(wx_next_event, evtName);
            snprintf(buf, sizeof(buf), "in %d days  |  %s",
                     minD, doy_to_date_str(nextDoy, year));
            lv_label_set_text(wx_next_event_sub, buf);

            // Season (Southern Hemisphere)
            const char *seasonName = (doy>=e.dec||doy<e.mar) ? "Summer" :
                                     (doy<e.jun)              ? "Autumn" :
                                     (doy<e.sep)              ? "Winter" : "Spring";
            lv_label_set_text(wx_season, seasonName);
        }
    }

    // Updated time
    {
        time_t now4 = time(nullptr);
        struct tm *t4 = localtime(&now4);
        snprintf(buf, sizeof(buf), "Updated %02d:%02d", t4->tm_hour, t4->tm_min);
        lv_label_set_text(wx_updated, buf);
    }


    // Screen 3 — Forecast cards (fully rebuilt each update)
    for (int i = 0; i < 3; i++) {
        if (!fc_cards[i]) continue;
        lv_obj_clean(fc_cards[i]);
        lv_obj_set_style_pad_all(fc_cards[i], PAD, 0);

        const ForecastDay &f = wr.forecast[i];
        // Use known card dimensions — don't query after lv_obj_clean as
        // LVGL hasn't re-laid out the object yet and returns stale/zero values
        const int CARD_W  = (SCREEN_W - NAV_BTN_W * 2 - PAD * 4) / 3;
        const int CARD_H  = CONTENT_H - PAD * 2;
        const int CW      = CARD_W - PAD * 2;   // interior width
        const int CH      = CARD_H - PAD * 2;   // interior height

        // ── helper: make a label inside the card at absolute y ───────────────
        auto fc_lbl = [&](const char *txt, const lv_font_t *font,
                          lv_color_t col, int y) -> lv_obj_t* {
            lv_obj_t *l = lv_label_create(fc_cards[i]);
            lv_label_set_text(l, txt);
            lv_obj_set_style_text_font(l, font, 0);
            lv_obj_set_style_text_color(l, col, 0);
            lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);
            return l;
        };

        // ── Day name + date ───────────────────────────────────────────────────
        char line[64];
        fc_lbl(i == 0 ? "Today" : f.dayName,
               &lv_font_montserrat_20, C_ACCENT, 0);

        // Date string from "2025-03-15" → "15 Mar 2025"
        {
            int yr, mo, dy;
            sscanf(f.date, "%d-%d-%d", &yr, &mo, &dy);
            static const char *mnames[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                            "Jul","Aug","Sep","Oct","Nov","Dec"};
            snprintf(line, sizeof(line), "%d %s %d", dy,
                     (mo >= 1 && mo <= 12) ? mnames[mo-1] : "?", yr);
            fc_lbl(line, &lv_font_montserrat_14, C_DIM, 24);
        }

        // ── Divider ───────────────────────────────────────────────────────────
        lv_obj_t *div = lv_obj_create(fc_cards[i]);
        lv_obj_set_size(div, CW, 1);
        lv_obj_set_pos(div, 0, 44);
        lv_obj_set_style_bg_color(div, C_BORDER, 0);
        lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(div, 0, 0);
        lv_obj_set_style_pad_all(div, 0, 0);
        lv_obj_clear_flag(div, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // ── Hero — big temp + weather icon ───────────────────────────────────
        const int HERO_Y = 52;

        // Weather icon canvas — right side of hero
        lv_color_t *icbuf = (lv_color_t *)malloc(
            OUTLOOK_ICON_W * OUTLOOK_ICON_H * sizeof(lv_color_t));
        if (icbuf) {
            lv_obj_t *ic = lv_canvas_create(fc_cards[i]);
            lv_canvas_set_buffer(ic, icbuf, OUTLOOK_ICON_W, OUTLOOK_ICON_H,
                                 LV_IMG_CF_TRUE_COLOR);
            lv_obj_align(ic, LV_ALIGN_TOP_RIGHT, 0, HERO_Y);
            lv_canvas_fill_bg(ic, C_CARD, LV_OPA_COVER);
            draw_wx_icon(ic, wx_icon_category(f.conditionCode));
        }

        // Max temp — large
        snprintf(line, sizeof(line), "%.0f\xC2\xB0", f.maxTempC);
        fc_lbl(line, &lv_font_montserrat_48, C_TEXT, HERO_Y);

        // Hi / Lo range
        snprintf(line, sizeof(line), "Hi %.0f\xC2\xB0  Lo %.0f\xC2\xB0",
                 f.maxTempC, f.minTempC);
        fc_lbl(line, &lv_font_montserrat_14, C_DIM, HERO_Y + 62);

        // Condition
        fc_lbl(f.condition, &lv_font_montserrat_16, C_ACCENT, HERO_Y + 82);

        // ── 3×2 stat grid ────────────────────────────────────────────────────
        // Starts below condition line, leaves room for rain bar at bottom
        const int RAIN_H  = 40;
        const int GRID_Y  = HERO_Y + 108;
        const int GRID_H  = CH - GRID_Y - RAIN_H - 8;
        const int ROW_H   = GRID_H / 3;
        const int COL_W   = CW / 2;

        // Grid border lines
        // Vertical centre line
        lv_obj_t *vl = lv_obj_create(fc_cards[i]);
        lv_obj_set_size(vl, 1, GRID_H);
        lv_obj_set_pos(vl, COL_W, GRID_Y);
        lv_obj_set_style_bg_color(vl, lv_color_hex(0x21262D), 0);
        lv_obj_set_style_bg_opa(vl, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(vl, 0, 0);
        lv_obj_set_style_pad_all(vl, 0, 0);
        lv_obj_clear_flag(vl, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // Two horizontal dividers between rows
        for (int r = 1; r < 3; r++) {
            lv_obj_t *hl = lv_obj_create(fc_cards[i]);
            lv_obj_set_size(hl, CW, 1);
            lv_obj_set_pos(hl, 0, GRID_Y + r * ROW_H);
            lv_obj_set_style_bg_color(hl, lv_color_hex(0x21262D), 0);
            lv_obj_set_style_bg_opa(hl, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(hl, 0, 0);
            lv_obj_set_style_pad_all(hl, 0, 0);
            lv_obj_clear_flag(hl, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }

        // Stat data: { label, value_string, colour }
        struct StatCell { const char *lbl; char val[24]; lv_color_t col; };
        StatCell cells[6];

        cells[0] = {"Precip", "", C_COOL};
        snprintf(cells[0].val, 24, "%.1f mm", f.precipMM);

        cells[1] = {"Humidity", "", C_COOL};
        snprintf(cells[1].val, 24, "%d%%", f.avgHumidity);

        cells[2] = {"Max wind", "", C_TEXT};
        snprintf(cells[2].val, 24, "%.0f km/h", f.maxWindKph);

        cells[3] = {"UV Index", "", C_YELLOW};
        if (f.uvIndex >= 8)       cells[3].col = C_RED;
        else if (f.uvIndex >= 3)  cells[3].col = C_YELLOW;
        else                      cells[3].col = C_GREEN;
        snprintf(cells[3].val, 24, "%.1f", f.uvIndex);

        cells[4] = {"Sunrise", "", lv_color_hex(0xD29922)};
        snprintf(cells[4].val, 24, "%s", f.sunrise);

        cells[5] = {"Sunset", "", C_WARM};
        snprintf(cells[5].val, 24, "%s", f.sunset);

        for (int c = 0; c < 6; c++) {
            int col = c % 2;
            int row = c / 2;
            int cx  = col * COL_W + 6;
            int ry  = GRID_Y + row * ROW_H + 6;

            // Label
            lv_obj_t *lbl = lv_label_create(fc_cards[i]);
            lv_label_set_text(lbl, cells[c].lbl);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl, C_DIM, 0);
            lv_obj_set_pos(lbl, cx, ry);

            // Value
            lv_obj_t *val = lv_label_create(fc_cards[i]);
            lv_label_set_text(val, cells[c].val);
            lv_obj_set_style_text_font(val, &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(val, cells[c].col, 0);
            lv_obj_set_pos(val, cx, ry + 18);
        }

        // ── Rain chance bar ───────────────────────────────────────────────────
        const int BAR_Y = CH - RAIN_H + 4;

        // Label + percentage on one line
        lv_obj_t *rl = lv_label_create(fc_cards[i]);
        lv_label_set_text(rl, "Rain chance");
        lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(rl, C_DIM, 0);
        lv_obj_set_pos(rl, 0, BAR_Y);

        snprintf(line, sizeof(line), "%d%%", f.rainChancePct);
        lv_obj_t *rp = lv_label_create(fc_cards[i]);
        lv_label_set_text(rp, line);
        lv_obj_set_style_text_font(rp, &lv_font_montserrat_16, 0);
        lv_color_t rc = f.rainChancePct >= 60 ? C_ACCENT : C_COOL;
        if (f.rainChancePct < 20) rc = C_DIM;
        lv_obj_set_style_text_color(rp, rc, 0);
        lv_obj_align(rp, LV_ALIGN_TOP_RIGHT, 0, BAR_Y);

        // Bar track
        lv_obj_t *bt = lv_obj_create(fc_cards[i]);
        lv_obj_set_size(bt, CW, 6);
        lv_obj_set_pos(bt, 0, BAR_Y + 20);
        lv_obj_set_style_bg_color(bt, lv_color_hex(0x21262D), 0);
        lv_obj_set_style_bg_opa(bt, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(bt, 0, 0);
        lv_obj_set_style_radius(bt, 3, 0);
        lv_obj_set_style_pad_all(bt, 0, 0);
        lv_obj_clear_flag(bt, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        // Bar fill
        int fill_w = (int)((float)f.rainChancePct / 100.0f * CW);
        if (fill_w > 0) {
            lv_obj_t *bf = lv_obj_create(fc_cards[i]);
            lv_obj_set_size(bf, fill_w, 6);
            lv_obj_set_pos(bf, 0, BAR_Y + 20);
            lv_obj_set_style_bg_color(bf, rc, 0);
            lv_obj_set_style_bg_opa(bf, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bf, 0, 0);
            lv_obj_set_style_radius(bf, 3, 0);
            lv_obj_set_style_pad_all(bf, 0, 0);
            lv_obj_clear_flag(bf, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        }
    }

    // Screen 4 — Astronomy (time-sensitive: delegated to render_astronomy,
    // called every minute from ui_update_celestial with a fresh time read)
    render_astronomy(wr);
}

// ── Astronomy screen update — called every minute so countdowns + arc stay live ─
// wr supplies the weather data (sunrise/sunset strings, moon info) from the last
// weather fetch.  All time-of-day arithmetic uses a fresh time() call so the arc
// and countdowns are accurate regardless of when the last weather fetch occurred.
static void render_astronomy(const WeatherResult &wr) {
    if (!as_sunrise) return;
    if (!wr.valid)   return;   // no weather data yet

    char buf2[48];
    time_t now_t  = time(nullptr);
    struct tm *lt = localtime(&now_t);
    int curMin    = lt->tm_hour * 60 + lt->tm_min;

    int srMins  = parse_time_to_mins(wr.sunrise);
    int ssMins  = parse_time_to_mins(wr.sunset);
    int noonMin = (srMins + ssMins) / 2;
    int dayLen  = ssMins - srMins;

    // Countdown formatter
    auto fmt_cd = [](int diff, char *out, size_t n) {
        int ad = diff < 0 ? -diff : diff;
        const char *dir = diff < 0 ? "ago" : "away";
        if (ad < 60)  snprintf(out, n, "%dm %s", ad, dir);
        else          snprintf(out, n, "%dh %dm %s", ad/60, ad%60, dir);
    };

    // ── Sun rows ──────────────────────────────────────────────────────────────
    lv_label_set_text(as_sunrise, wr.sunrise);
    fmt_cd(srMins - curMin, buf2, sizeof(buf2));
    lv_label_set_text(as_sunrise_sub, buf2);

    lv_label_set_text(as_solarnoon, mins_to_time_str(noonMin));
    fmt_cd(noonMin - curMin, buf2, sizeof(buf2));
    lv_label_set_text(as_solarnoon_sub, buf2);

    lv_label_set_text(as_sunset, wr.sunset);
    fmt_cd(ssMins - curMin, buf2, sizeof(buf2));
    lv_label_set_text(as_sunset_sub, buf2);

    snprintf(buf2, sizeof(buf2), "%dh %dm", dayLen/60, dayLen%60);
    lv_label_set_text(as_daylen, buf2);

    // Next equinox/solstice
    {
        int year = lt->tm_year + 1900;
        SeasonEvts e = calc_season_events(year);
        int doy = day_of_year(lt->tm_mday, lt->tm_mon + 1, year);
        int dMar = ((e.mar-doy)%365+365)%365; if(!dMar) dMar=365;
        int dJun = ((e.jun-doy)%365+365)%365; if(!dJun) dJun=365;
        int dSep = ((e.sep-doy)%365+365)%365; if(!dSep) dSep=365;
        int dDec = ((e.dec-doy)%365+365)%365; if(!dDec) dDec=365;
        int minD = dMar;
        if (dJun < minD) minD = dJun;
        if (dSep < minD) minD = dSep;
        if (dDec < minD) minD = dDec;
        const char *evtName = (dMar==minD) ? "Mar Equinox"  :
                              (dJun==minD) ? "Jun Solstice" :
                              (dSep==minD) ? "Sep Equinox"  : "Dec Solstice";
        int nextDoy = (dMar==minD)?e.mar:(dJun==minD)?e.jun:(dSep==minD)?e.sep:e.dec;
        lv_label_set_text(as_next_event, evtName);
        snprintf(buf2, sizeof(buf2), "in %d days  %s", minD,
                 doy_to_date_str(nextDoy, year));
        lv_label_set_text(as_next_event_sub, buf2);

        const char *seasonName = (doy>=e.dec||doy<e.mar) ? "Summer" :
                                 (doy<e.jun)              ? "Autumn" :
                                 (doy<e.sep)              ? "Winter" : "Spring";
        lv_label_set_text(as_season, seasonName);
    }

    // ── Sun arc canvas ────────────────────────────────────────────────────────
    if (as_sun_canvas && as_sun_buf) {
        lv_canvas_fill_bg(as_sun_canvas, C_CARD, LV_OPA_COVER);
        const int W = AS_ARC_W, H = AS_ARC_H;
        const int cx  = W / 2;
        const int cy2 = H - 20;   // horizon y
        // Ellipse radii: rx spans exactly sunrise-tick to sunset-tick (mX to W-mX),
        // ry keeps the arc apex on-canvas with room for the sun icon (22px rays).
        const int mX = 36;
        const int rx = cx - mX;   // 210 — arc touches the E/W tick marks exactly
        const int ry = cy2 - 24;  // 134 — apex at y=24, icon rays clear top by 2px

        // Horizon line
        canvas_line(as_sun_canvas, mX, cy2, W - mX, cy2, C_BORDER, 1);

        // Noon dashed vertical
        for (int yy = cy2 - ry; yy < cy2; yy += 8)
            canvas_line(as_sun_canvas, cx, yy, cx, yy + 4,
                        lv_color_hex(0x21262D), 1);

        // Full arc track — ellipse from E (deg=0) to W (deg=180)
        for (int deg = 0; deg <= 179; deg++) {
            float r1 = deg       * (float)M_PI / 180.0f;
            float r2 = (deg + 1) * (float)M_PI / 180.0f;
            int x1 = cx - (int)(rx * cosf(r1));
            int y1 = cy2 - (int)(ry * sinf(r1));
            int x2 = cx - (int)(rx * cosf(r2));
            int y2 = cy2 - (int)(ry * sinf(r2));
            canvas_line(as_sun_canvas, x1, y1, x2, y2, lv_color_hex(0x1C3A5E), 2);
        }

        // Sunrise / Sunset tick marks
        canvas_line(as_sun_canvas, mX,   cy2, mX,   cy2 - 10, C_DIM, 2);
        canvas_line(as_sun_canvas, W-mX, cy2, W-mX, cy2 - 10, C_DIM, 2);

        bool isDaytime = (curMin >= srMins && curMin <= ssMins);
        if (isDaytime) {
            float prog   = (float)(curMin - srMins) / (float)(dayLen > 0 ? dayLen : 1);
            float sunDeg = prog * 180.0f;
            float sunRad = sunDeg * (float)M_PI / 180.0f;
            int sx = cx - (int)(rx * cosf(sunRad));
            int sy = cy2 - (int)(ry * sinf(sunRad));
            // Travelled arc highlight
            for (int deg = 0; deg <= (int)sunDeg; deg++) {
                float r1 = deg       * (float)M_PI / 180.0f;
                float r2 = (deg + 1) * (float)M_PI / 180.0f;
                int x1 = cx - (int)(rx*cosf(r1)), y1 = cy2 - (int)(ry*sinf(r1));
                int x2 = cx - (int)(rx*cosf(r2)), y2 = cy2 - (int)(ry*sinf(r2));
                canvas_line(as_sun_canvas, x1, y1, x2, y2,
                            lv_color_hex(0xD29922), 2);
            }
            draw_sun_icon(as_sun_canvas, sx, sy, 11,
                          lv_color_hex(0xFFB700), C_CARD);
        } else {
            // Night — crescent moon near horizon right
            canvas_stroke_circle(as_sun_canvas, W - mX - 22, cy2 + 14, 10,
                                 C_COOL, 2);
        }
    }

    // ── Moon rows ─────────────────────────────────────────────────────────────
    lv_label_set_text(as_moon_phase, wr.moonPhase);
    snprintf(buf2, sizeof(buf2), "%d%% illuminated", wr.moonIllumPct);
    lv_label_set_text(as_moon_illum, buf2);

    float age = lunar_age_now();
    snprintf(buf2, sizeof(buf2), "Age: %.1f days", age);
    lv_label_set_text(as_moon_age, buf2);

    lv_label_set_text(as_moonrise, wr.moonrise);
    lv_label_set_text(as_moonset,  wr.moonset);

    {
        int mrMins = parse_time_to_mins(wr.moonrise);
        int msMins = parse_time_to_mins(wr.moonset);
        fmt_cd(mrMins - curMin, buf2, sizeof(buf2));
        lv_label_set_text(as_moonrise_sub, buf2);
        fmt_cd(msMins - curMin, buf2, sizeof(buf2));
        lv_label_set_text(as_moonset_sub, buf2);
    }

    snprintf(buf2, sizeof(buf2), "%d%%", wr.moonIllumPct);
    lv_label_set_text(as_moon_illum_pct, buf2);

    snprintf(buf2, sizeof(buf2), "%.1f days", age);
    lv_label_set_text(as_moon_age_val, buf2);

    float angle = (age / 29.53f) * 360.0f;
    {
        float dToFull = (180.0f - fmodf(angle, 360.0f));
        if (dToFull < 0) dToFull += 360.0f;
        int dFull = (int)(dToFull / 360.0f * 29.53f) + 1;
        snprintf(buf2, sizeof(buf2), "in %d days", dFull);
        lv_label_set_text(as_next_full, buf2);

        float dToNew = (360.0f - fmodf(angle, 360.0f));
        if (dToNew >= 360.0f) dToNew -= 360.0f;
        int dNew = (int)(dToNew / 360.0f * 29.53f) + 1;
        snprintf(buf2, sizeof(buf2), "in %d days", dNew);
        lv_label_set_text(as_next_new, buf2);
    }

    // Moon disc canvas
    if (as_moon_canvas && as_moon_buf) {
        lv_canvas_fill_bg(as_moon_canvas, C_CARD, LV_OPA_COVER);
        int moonR = AS_MOON_D / 2 - 4;
        draw_moon_disc(as_moon_canvas,
                       AS_MOON_D / 2, AS_MOON_D / 2, moonR,
                       age,
                       lv_color_hex(0xCDD9E5), C_CARD,
                       lv_color_hex(0x1A1E24));
        canvas_stroke_circle(as_moon_canvas,
                             AS_MOON_D / 2, AS_MOON_D / 2, moonR,
                             C_DIM, 1);
    }
}

// ── Status bar update ─────────────────────────────────────────────────────────
void ui_update_statusbar() {
    bool connected = (WiFi.status() == WL_CONNECTED);
    int  rssi      = connected ? WiFi.RSSI() : 0;

    // ── Power state detection ─────────────────────────────────────────────────
    // Without a battery installed, the PMIC sees a floating rail and oscillates
    // between charging/not-charging and 0%/100%. We stabilise this by:
    // 1. Checking voltage — a real battery always reads 3200–4200mV.
    //    Below 3000mV (or 0) with USB connected = no battery fitted.
    // 2. Smoothing the isCharging() state with a counter to avoid icon flicker.

    int  bat_mv    = M5.Power.getBatteryVoltage();  // millivolts — 2S pack: 5500–8400mV
    int  lvl       = M5.Power.getBatteryLevel();    // 0–100
    bool raw_chrg  = M5.Power.isCharging();

    // ── No-battery detection ──────────────────────────────────────────────────
    // Tab5 uses a 7.2V 2S Li-ion pack (FB-NP-F550-B):
    //   Charge limit: 8400mV  |  Discharge cutoff: 5500mV  |  Nominal: 7200mV
    // WITHOUT a battery, the PMIC oscillates ~4280mV ↔ ~8380mV every second.
    // The 8380mV reading is dangerously close to the 8400mV charge limit —
    // so we use 8300mV as the upper bound to exclude that oscillation peak.
    // Additionally we require 10 consecutive stable readings (10 seconds) before
    // declaring battery present, and flag any swing > 500mV as unstable.
    static int  s_prev_mv      = 0;
    static bool s_no_battery   = true;
    static int  s_stable_count = 0;

    bool out_of_range = (bat_mv < 5500 || bat_mv > 8300);
    int  swing        = abs(bat_mv - s_prev_mv);
    bool unstable     = (s_prev_mv > 0 && swing > 500);

    if (out_of_range || unstable) {
        s_no_battery   = true;
        s_stable_count = 0;
    } else {
        s_stable_count++;
        if (s_stable_count >= 10) {
            s_no_battery = false;
        }
    }
    s_prev_mv = bat_mv;
    bool no_battery = s_no_battery;

    // Smooth isCharging() — require 3 consecutive same readings before switching.
    // Also force charging=false when no battery detected — isCharging() is
    // unreliable without a battery and causes spurious "Charging" display.
    static bool s_charging   = false;
    static int  s_chrg_count = 0;
    bool raw_chrg_use = no_battery ? false : raw_chrg;  // suppress when no battery
    if (raw_chrg_use != s_charging) {
        s_chrg_count++;
        if (s_chrg_count >= 3) { s_charging = raw_chrg_use; s_chrg_count = 0; }
    } else {
        s_chrg_count = 0;
    }
    bool charging = no_battery ? false : s_charging;

    // Battery icon symbol + colour
    const char *bat_sym;
    lv_color_t  bat_col;
    char        bat_pct_buf[8];

    if (no_battery) {
        // USB power only — show lightning bolt, dim colour, "USB" label
        bat_sym = LV_SYMBOL_CHARGE;
        bat_col = C_DIM;
        snprintf(bat_pct_buf, sizeof(bat_pct_buf), "USB");
    } else if (charging) {
        bat_sym = LV_SYMBOL_CHARGE;
        bat_col = C_COOL;
        snprintf(bat_pct_buf, sizeof(bat_pct_buf), "%d%%", lvl);
    } else if (lvl < 10) {
        bat_sym = LV_SYMBOL_BATTERY_EMPTY;
        bat_col = C_RED;
        snprintf(bat_pct_buf, sizeof(bat_pct_buf), "%d%%", lvl);
    } else if (lvl < 40) {
        bat_sym = LV_SYMBOL_BATTERY_1;
        bat_col = C_YELLOW;
        snprintf(bat_pct_buf, sizeof(bat_pct_buf), "%d%%", lvl);
    } else if (lvl < 75) {
        bat_sym = LV_SYMBOL_BATTERY_3;
        bat_col = C_YELLOW;
        snprintf(bat_pct_buf, sizeof(bat_pct_buf), "%d%%", lvl);
    } else {
        bat_sym = LV_SYMBOL_BATTERY_FULL;
        bat_col = C_GREEN;
        snprintf(bat_pct_buf, sizeof(bat_pct_buf), "%d%%", lvl);
    }

    // RSSI text + colour
    char rssi_buf[10] = "--";
    lv_color_t rssi_col = C_DIM;
    if (connected) {
        snprintf(rssi_buf, sizeof(rssi_buf), "%d", rssi);
        rssi_col = rssi > -65 ? C_GREEN : rssi > -75 ? C_YELLOW : C_RED;
    }

    // Clock HH:MM from system time
    char time_buf[8];
    time_t now_t = time(nullptr);
    struct tm *lt = localtime(&now_t);
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", lt->tm_hour, lt->tm_min);

    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (sb_wifi[i]) {
            lv_obj_set_style_text_color(sb_wifi[i],
                connected ? C_GREEN : C_RED, 0);
        }
        if (sb_rssi[i]) {
            lv_label_set_text(sb_rssi[i], rssi_buf);
            lv_obj_set_style_text_color(sb_rssi[i], rssi_col, 0);
        }
        if (sb_bat[i]) {
            lv_label_set_text(sb_bat[i], bat_sym);
            lv_obj_set_style_text_color(sb_bat[i], bat_col, 0);
        }
        if (sb_bat_pct[i]) {
            lv_label_set_text(sb_bat_pct[i], bat_pct_buf);
            lv_obj_set_style_text_color(sb_bat_pct[i], bat_col, 0);
        }
        if (sb_time[i]) {
            lv_label_set_text(sb_time[i], time_buf);
        }
    }
}

// ── Statusbar sensor update — called from ui_update_local ─────────────────────
void ui_update_statusbar_sensors(const SensorData &sd) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%.1fC", sd.tempC);  // no degree glyph in Montserrat_14
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (sb_temp[i])  lv_label_set_text(sb_temp[i], buf);
    }
    snprintf(buf, sizeof(buf), "%.0f%%", sd.humidity);
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (sb_hum[i])   lv_label_set_text(sb_hum[i], buf);
    }
    snprintf(buf, sizeof(buf), "%.0fhPa", sd.pressureHPa);
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (sb_press[i]) lv_label_set_text(sb_press[i], buf);
    }
}

// ── Celestial screens update ──────────────────────────────────────────────────
// Called every minute — re-renders seasons, lunar, and astronomy (arc + countdowns).
void ui_update_celestial(const RtcDateTime &dt, const WeatherResult &wr) {
    render_seasons(dt);
    render_lunar(wr);
    render_astronomy(wr);   // keeps sun arc position and all countdowns live every minute
    render_solar(dt, wr);   // solar elevation chart — sun position + stat row
}
