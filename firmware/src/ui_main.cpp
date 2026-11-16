// Tileview + clock, PC monitor and info screens, overlays and the screen policy
// (night mode, PC asleep, idle eye).
#include <WiFi.h>
#include <time.h>
#include "app.h"
#include "theme.h"

static lv_obj_t *tv, *dots[TILE_COUNT];

// clock
static lv_obj_t *clkMeter, *lblDate, *lblTime, *lblWxIcon, *lblWxTemp, *lblWxDesc, *lblCity, *lblNoWifi;
static lv_meter_indicator_t* clkSec;

// monitor
static lv_obj_t *monMeter, *lblCpu, *lblHost;
static lv_meter_indicator_t *iNorm, *iWarn, *iHot;
struct StatCol {
  lv_obj_t *val, *bar;
};
static StatCol colGpu, colRam, colTemp;
static float shownCpu = 0, shownGpu = 0, shownRam = 0, shownTemp = 0;

// info
static lv_obj_t *lblIp, *lblWifi, *lblUp, *slider;

// overlays
static lv_obj_t *otaLayer, *otaArc, *otaPct, *portalLayer, *otaBlank, *otaPrevScr;
static uint32_t otaLastMs = 0;
static bool forceEye = false;

// ================================================================= helpers
static bool timeValid(struct tm& ti) { return getLocalTime(&ti, 0) && ti.tm_year > 120; }

bool isNight() {
  struct tm ti;
  if (!timeValid(ti)) return false;
  return NIGHT_START_HOUR > NIGHT_END_HOUR
             ? (ti.tm_hour >= NIGHT_START_HOUR || ti.tm_hour < NIGHT_END_HOUR)
             : (ti.tm_hour >= NIGHT_START_HOUR && ti.tm_hour < NIGHT_END_HOUR);
}

static const char* wxIcon(int c, bool day) {
  if (c == 0) return day ? ICON_SUN : ICON_MOON;
  if (c <= 2) return day ? ICON_PARTLY : ICON_MOON;
  if (c == 3) return ICON_CLOUD;
  if (c <= 48) return ICON_FOG;
  if (c <= 57) return ICON_DRIZZLE;
  if (c <= 67 || (c >= 80 && c <= 82)) return ICON_RAIN;
  if (c <= 77 || c == 85 || c == 86) return ICON_SNOW;
  return ICON_STORM;
}

static const char* wxText(int c) {
  if (c == 0) return "Despejado";
  if (c == 1) return "Casi despejado";
  if (c == 2) return "Poco nuboso";
  if (c == 3) return "Nublado";
  if (c <= 48) return "Niebla";
  if (c <= 57) return "Llovizna";
  if (c <= 67) return "Lluvia";
  if (c <= 77) return "Nieve";
  if (c <= 82) return "Chubascos";
  if (c <= 86) return "Nevadas";
  return "Tormenta";
}

// ================================================================= clock
static void buildClock(lv_obj_t* t) {
  lv_meter_scale_t* sc;
  clkMeter = uiTickMeter(t, &sc, 60, 5, 2, 7, 13, 59, 354, 270);
  clkSec = lv_meter_add_scale_lines(clkMeter, sc, C_ACCENT, C_ACCENT, false, 0);
  lv_meter_set_indicator_start_value(clkMeter, clkSec, 0);

  lblNoWifi = uiLabel(t, &icons_16, C_HOT, ICON_WIFI_OFF);
  lv_obj_align(lblNoWifi, LV_ALIGN_CENTER, 0, -72);

  lblDate = uiCaption(t, "");
  lv_obj_align(lblDate, LV_ALIGN_CENTER, 0, -48);
  lblTime = uiLabel(t, &font_clock, C_TXT, "--:--");
  lv_obj_align(lblTime, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t* row = uiRow(t, 6);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, 36);
  lblWxIcon = uiLabel(row, &icons_24, C_ACCENT, "");
  lblWxTemp = uiLabel(row, &font_value, C_TXT, "");
  lblWxDesc = uiLabel(t, &font_small, C_TXT2, "");
  lv_obj_align(lblWxDesc, LV_ALIGN_CENTER, 0, 60);
  lblCity = uiCaption(t, "", C_TXT3);
  lv_obj_align(lblCity, LV_ALIGN_CENTER, 0, 80);
}

static void tickClock() {
  static const char* DAYS[] = {"DOMINGO", "LUNES", "MARTES", "MIÉRCOLES", "JUEVES", "VIERNES", "SÁBADO"};
  static const char* MON[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                              "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
  char b[64];
  struct tm ti;
  if (timeValid(ti)) {
    snprintf(b, sizeof b, "%02d:%02d", ti.tm_hour, ti.tm_min);
    uiSetText(lblTime, b);
    snprintf(b, sizeof b, "%s · %d %s", DAYS[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon]);
    uiSetText(lblDate, b);
    static int lastSec = -1;
    if (ti.tm_sec != lastSec) {
      lastSec = ti.tm_sec;
      lv_meter_set_indicator_end_value(clkMeter, clkSec, ti.tm_sec);
    }
  }
  if (weather.valid) {
    uiSetText(lblWxIcon, wxIcon(weather.code, weather.isDay));
    snprintf(b, sizeof b, "%d°", (int)lroundf(weather.temp));
    uiSetText(lblWxTemp, b);
    snprintf(b, sizeof b, "%s · %d° / %d°", wxText(weather.code), (int)lroundf(weather.tmin),
             (int)lroundf(weather.tmax));
    uiSetText(lblWxDesc, b);
    String city = weather.city;
    city.toUpperCase();
    uiSetText(lblCity, city.c_str());
  }
  if (WiFi.isConnected()) lv_obj_add_flag(lblNoWifi, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(lblNoWifi, LV_OBJ_FLAG_HIDDEN);
}

// ================================================================= monitor
static StatCol makeCol(lv_obj_t* t, int x, const char* cap) {
  lv_obj_t* c = uiBox(t);
  lv_obj_set_size(c, 52, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(c, 3, 0);
  lv_obj_align(c, LV_ALIGN_CENTER, x, 50);
  uiCaption(c, cap, C_TXT3);
  StatCol s;
  s.val = uiLabel(c, &font_body, C_TXT, "--");
  s.bar = uiBar(c, 32, 3, C_ACCENT);
  return s;
}

static void buildMonitor(lv_obj_t* t) {
  lv_meter_scale_t* sc;
  monMeter = uiTickMeter(t, &sc, 51, 5, 3, 11, 17, 100, 270, 135);
  lv_meter_indicator_t* red = lv_meter_add_scale_lines(monMeter, sc, C_HOT_DIM, C_HOT_DIM, false, 0);
  lv_meter_set_indicator_start_value(monMeter, red, 90);
  lv_meter_set_indicator_end_value(monMeter, red, 100);
  iNorm = lv_meter_add_scale_lines(monMeter, sc, C_ACCENT, C_ACCENT, false, 0);
  iWarn = lv_meter_add_scale_lines(monMeter, sc, C_WARN, C_WARN, false, 0);
  iHot = lv_meter_add_scale_lines(monMeter, sc, C_HOT, C_HOT, false, 0);
  lv_meter_set_indicator_start_value(monMeter, iWarn, 76);
  lv_meter_set_indicator_start_value(monMeter, iHot, 90);

  lv_obj_t* cap = uiCaption(t, "CPU");
  lv_obj_align(cap, LV_ALIGN_CENTER, 0, -50);
  lv_obj_t* row = uiRow(t, 2);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, -14);
  lblCpu = uiLabel(row, &font_num, C_TXT, "--");
  lv_obj_t* pct = uiLabel(row, &font_value, C_TXT3, "%");
  lv_obj_set_style_pad_bottom(pct, 9, 0);

  lv_obj_t* div = uiBox(t);
  lv_obj_set_size(div, 120, 1);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(div, C_SURFACE2, 0);
  lv_obj_align(div, LV_ALIGN_CENTER, 0, 22);

  colGpu = makeCol(t, -54, "GPU");
  colRam = makeCol(t, 0, "RAM");
  colTemp = makeCol(t, 54, "TEMP");

  lblHost = uiCaption(t, "", C_TXT3);
  lv_obj_align(lblHost, LV_ALIGN_CENTER, 0, 92);
}

static void setCol(StatCol& c, float v, float barPct, const char* fmt) {
  char b[16];
  snprintf(b, sizeof b, fmt, (int)lroundf(v));
  uiSetText(c.val, b);
  lv_bar_set_value(c.bar, (int)(constrain(barPct, 0, 100) * 10), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(c.bar, uiLoadColor(barPct, C_ACCENT), LV_PART_INDICATOR);
}

static void fastMonitor() {
  bool on = pcOnline();
  shownCpu += ((on ? pc.cpu : 0) - shownCpu) * 0.2f;
  shownGpu += ((on ? pc.gpu : 0) - shownGpu) * 0.2f;
  shownRam += ((on ? pc.ram : 0) - shownRam) * 0.2f;
  shownTemp += ((on ? pc.gpuTemp : 0) - shownTemp) * 0.2f;
  // the meter redraws all 51 ticks on every set -> only touch it when a tick changes
  static int lastV = -1;
  int v = (int)lroundf(shownCpu) / 2 * 2;
  if (v != lastV) {
    lastV = v;
    lv_meter_set_indicator_end_value(monMeter, iNorm, min(v, 75));
    lv_meter_set_indicator_end_value(monMeter, iWarn, v >= 76 ? min(v, 89) : -1);
    lv_meter_set_indicator_end_value(monMeter, iHot, v >= 90 ? v : -1);
  }
  if (on) {
    setCol(colGpu, pc.gpu, shownGpu, "%d%%");
    setCol(colRam, pc.ram, shownRam, "%d%%");
    // 30 °C -> empty bar, 90 °C -> full
    setCol(colTemp, pc.gpuTemp, (shownTemp - 30) * 100 / 60, "%d°");
  }
}

static void tickMonitor() {
  bool on = pcOnline();
  char b[8];
  if (on) {
    snprintf(b, sizeof b, "%d", (int)lroundf(pc.cpu));
    uiSetText(lblCpu, b);
    String h = pc.host;
    h.toUpperCase();
    uiSetText(lblHost, h.c_str());
  } else {
    uiSetText(lblCpu, "--");
    for (StatCol* c : {&colGpu, &colRam, &colTemp}) {
      uiSetText(c->val, "--");
      lv_bar_set_value(c->bar, 0, LV_ANIM_OFF);
    }
    uiSetText(lblHost, WiFi.isConnected() ? "ESPERANDO AL PC" : "SIN WIFI");
  }
}

// ================================================================= info
static void sliderCb(lv_event_t* e) {
  lv_obj_t* s = lv_event_get_target(e);
  hwSetUserBrightness(lv_slider_get_value(s), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

static void buildInfo(lv_obj_t* t) {
  lv_obj_t* title = uiLabel(t, &font_body, C_TXT, "DeskBuddy");
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -70);
  lv_obj_t* ver = uiCaption(t, "FIRMWARE " FW_VERSION, C_TXT3);
  lv_obj_align(ver, LV_ALIGN_CENTER, 0, -50);

  lblIp = uiLabel(t, &font_small, C_TXT, "");
  lv_obj_align(lblIp, LV_ALIGN_CENTER, 0, -20);
  lblWifi = uiLabel(t, &font_small, C_TXT2, "");
  lv_obj_align(lblWifi, LV_ALIGN_CENTER, 0, -1);
  lblUp = uiLabel(t, &font_small, C_TXT2, "");
  lv_obj_align(lblUp, LV_ALIGN_CENTER, 0, 18);

  lv_obj_t* icon = uiLabel(t, &icons_16, C_TXT2, ICON_BRIGHT);
  lv_obj_align(icon, LV_ALIGN_CENTER, -72, 54);
  slider = lv_slider_create(t);
  lv_obj_remove_style_all(slider);
  lv_obj_set_size(slider, 112, 6);
  lv_obj_align(slider, LV_ALIGN_CENTER, 12, 54);
  lv_slider_set_range(slider, 10, 255);
  lv_slider_set_value(slider, hwUserBrightness(), LV_ANIM_OFF);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, C_SURFACE2, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, 3, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, 3, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_bg_color(slider, C_TXT, LV_PART_KNOB);
  lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(slider, 14);
  lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(slider, sliderCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(slider, sliderCb, LV_EVENT_RELEASED, NULL);

  lv_obj_t* host = uiCaption(t, HOSTNAME ".local", C_TXT3);
  lv_obj_set_style_text_letter_space(host, 1, 0);
  lv_obj_align(host, LV_ALIGN_CENTER, 0, 86);
}

static void tickInfo() {
  char b[64];
  if (WiFi.isConnected()) {
    uiSetText(lblIp, WiFi.localIP().toString().c_str());
    snprintf(b, sizeof b, "%s  ·  %d dBm", netSsid(), WiFi.RSSI());
    uiSetText(lblWifi, b);
  } else {
    uiSetText(lblIp, "Sin conexión");
    uiSetText(lblWifi, netSsid());
  }
  uint32_t m = millis() / 60000;
  if (m < 60) snprintf(b, sizeof b, "Encendido %lu min", m);
  else snprintf(b, sizeof b, "Encendido %lu h %02lu min", m / 60, m % 60);
  uiSetText(lblUp, b);
}

// ================================================================= overlays
static lv_obj_t* overlay() {
  lv_obj_t* o = uiBox(lv_layer_top());
  lv_obj_set_size(o, 240, 240);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, C_BG, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

static void otaWatchdog() {  // a failed upload never reboots: restore the UI
  if (!lv_obj_has_flag(otaLayer, LV_OBJ_FLAG_HIDDEN) && otaPrevScr && millis() - otaLastMs > 15000) {
    lv_obj_add_flag(otaLayer, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(otaPrevScr);
    otaPrevScr = NULL;
  }
}

static void buildOverlays() {
  otaBlank = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(otaBlank, C_BG, 0);
  otaLayer = overlay();
  otaArc = uiArc(otaLayer, 200, 6, C_ACCENT, 270, 360);
  lv_obj_t* icon = uiLabel(otaLayer, &icons_24, C_ACCENT, ICON_UPDATE);
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, -46);
  otaPct = uiLabel(otaLayer, &font_value, C_TXT, "0%");
  lv_obj_align(otaPct, LV_ALIGN_CENTER, 0, -6);
  lv_obj_t* cap = uiCaption(otaLayer, "ACTUALIZANDO");
  lv_obj_align(cap, LV_ALIGN_CENTER, 0, 24);

  portalLayer = overlay();
  lv_obj_t* wi = uiLabel(portalLayer, &icons_32, C_ACCENT, ICON_WIFI);
  lv_obj_align(wi, LV_ALIGN_CENTER, 0, -62);
  lv_obj_t* l1 = uiLabel(portalLayer, &font_body, C_TXT, "Configura el WiFi");
  lv_obj_align(l1, LV_ALIGN_CENTER, 0, -26);
  lv_obj_t* l2 = uiLabel(portalLayer, &font_small, C_TXT2, "Conéctate a la red");
  lv_obj_align(l2, LV_ALIGN_CENTER, 0, 2);
  lv_obj_t* l3 = uiLabel(portalLayer, &font_body, C_ACCENT, AP_NAME);
  lv_obj_align(l3, LV_ALIGN_CENTER, 0, 24);
  lv_obj_t* l4 = uiLabel(portalLayer, &font_small, C_TXT2, "desde el móvil");
  lv_obj_align(l4, LV_ALIGN_CENTER, 0, 46);
}

void uiOtaProgress(int pct) {
  static int last = -1;
  otaLastMs = millis();
  if (pct == last || (pct - last < 2 && pct < 100 && last >= 0 && pct > last)) return;
  last = pct;
  if (lv_obj_has_flag(otaLayer, LV_OBJ_FLAG_HIDDEN)) {
    // park on an empty screen so each progress refresh is cheap and the upload gets the CPU
    otaPrevScr = lv_scr_act();
    lv_scr_load(otaBlank);
  }
  lv_obj_clear_flag(otaLayer, LV_OBJ_FLAG_HIDDEN);
  lv_arc_set_value(otaArc, pct * 10);
  char b[8];
  snprintf(b, sizeof b, "%d%%", pct);
  lv_label_set_text(otaPct, b);
  hwSetTarget(hwUserBrightness());
  lv_refr_now(NULL);
}

void uiPortal(bool on) {
  if (on) lv_obj_clear_flag(portalLayer, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(portalLayer, LV_OBJ_FLAG_HIDDEN);
}

// ================================================================= tiles
int uiTile() { return lv_obj_get_x(lv_tileview_get_tile_act(tv)) / 240; }

static void tileChanged(lv_event_t*) {
  int idx = uiTile();
  for (int i = 0; i < TILE_COUNT; i++) {
    lv_obj_set_style_bg_color(dots[i], i == idx ? C_TXT : C_SURFACE2, 0);
    lv_obj_set_width(dots[i], i == idx ? 14 : 5);
  }
}

void uiGoTile(int i, bool anim) {
  if (i < 0 || i >= TILE_COUNT) return;
  lv_obj_set_tile_id(tv, i, 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
  tileChanged(NULL);
}

void uiOnMediaStarted() {
  if (millis() - lastTouchMs() > 15000 && uiTile() != TILE_MUSIC) uiGoTile(TILE_MUSIC);
}

void uiDebug(const char* cmd) {
  if (!strncmp(cmd, "tile ", 5)) {
    if (eyeActive()) eyeHide();
    forceEye = false;
    uiGoTile(atoi(cmd + 5), false);
  } else if (!strcmp(cmd, "eye")) {
    forceEye = true;
    eyeShow();
  } else if (!strcmp(cmd, "sleepy")) {
    forceEye = true;
    eyeShow();
    eyeSetSleepy(true);
  } else if (!strncmp(cmd, "ota ", 4)) {
    int p = atoi(cmd + 4);
    if (p >= 0) uiOtaProgress(p);
    else otaLastMs = 0, otaWatchdog();
  } else if (!strcmp(cmd, "alert")) {
    alertDebug();
  } else if (!strcmp(cmd, "alertmap")) {
    void alertDebugMap();
    alertDebugMap();
  } else if (!strcmp(cmd, "portal")) {
    uiPortal(lv_obj_has_flag(portalLayer, LV_OBJ_FLAG_HIDDEN));
  }
}

// ================================================================= policy
static void policy() {
  bool on = pcOnline();
  bool night = isNight();
  bool idle = on && pc.idle >= IDLE_EYE_SECONDS && !media.playing;

  // idle eye (an alert owns the screen while it's up)
  if (!forceEye && !alertActive()) {
    if (on && pc.idle < IDLE_EYE_SECONDS) eyeClearDismiss();
    if (idle && !eyeActive() && !eyeDismissed()) eyeShow();
    if (eyeActive() && !idle) eyeHide();
    if (eyeActive()) eyeSetSleepy(night || pc.idle >= IDLE_SLEEPY_SECONDS);
  }

  // PC asleep/off -> park on the clock
  static bool wasAsleep = false;
  bool asleep = pcAsleep();
  if (asleep && !wasAsleep && uiTile() != TILE_CLOCK) uiGoTile(TILE_CLOCK);
  wasAsleep = asleep;

  // backlight
  uint8_t b = hwUserBrightness();
  bool touched = millis() - lastTouchMs() < WAKE_ON_TOUCH_MS;
  if (night) b = min<uint8_t>(b, NIGHT_MAX_BRIGHTNESS);
  if (asleep && !touched) b = night ? 0 : min<uint8_t>(b, PC_OFF_BRIGHTNESS);
  if (eyeActive() && night && !touched) b = min<uint8_t>(b, 12);
  hwSetTarget(b);
}

static void fastTimer(lv_timer_t*) {
  static uint32_t last = millis();
  uint32_t now = millis(), dt = now - last;
  last = now;
  fastMonitor();
  musicFast();
  radarFast(dt);
  eyeTick();
}

static void slowTimer(lv_timer_t*) {
  tickClock();
  tickMonitor();
  musicTick();
  radarTick();
  alertTick();
  tickInfo();
  policy();
  otaWatchdog();
}

void uiInit() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_text_font(scr, &font_small, 0);

  tv = lv_tileview_create(scr);
  lv_obj_set_style_bg_color(tv, C_BG, 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t* tiles[TILE_COUNT];
  for (int i = 0; i < TILE_COUNT; i++) {
    lv_dir_t dir = (lv_dir_t)((i > 0 ? LV_DIR_LEFT : 0) | (i < TILE_COUNT - 1 ? LV_DIR_RIGHT : 0));
    tiles[i] = lv_tileview_add_tile(tv, i, 0, dir);
  }
  buildClock(tiles[TILE_CLOCK]);
  buildMonitor(tiles[TILE_MON]);
  musicBuild(tiles[TILE_MUSIC]);
  radarBuild(tiles[TILE_RADAR]);
  buildInfo(tiles[TILE_INFO]);
  alertBuild();

  lv_obj_t* dotRow = uiRow(scr, 4);
  lv_obj_set_flex_align(dotRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(dotRow, LV_ALIGN_BOTTOM_MID, 0, -9);
  for (int i = 0; i < TILE_COUNT; i++) {
    dots[i] = uiBox(dotRow);
    lv_obj_set_size(dots[i], 5, 5);
    lv_obj_set_style_radius(dots[i], 3, 0);
    lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
  }
  lv_obj_add_event_cb(tv, tileChanged, LV_EVENT_VALUE_CHANGED, NULL);
  uiGoTile(TILE_MON, false);

  buildOverlays();
  lv_timer_create(fastTimer, 30, NULL);
  lv_timer_create(slowTimer, 250, NULL);
  slowTimer(NULL);
}
