// Tileview + clock, PC monitor and info screens, overlays and the screen policy
// (night mode, PC asleep, idle eye).
#include <WiFi.h>
#include <time.h>
#include "app.h"
#include "hud.h"
#include "theme.h"

static lv_obj_t *tv, *dots[TILE_COUNT];


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

// ================================================================= clock (style C: phosphor CRT)
static lv_obj_t* clockView;

static const char* wxShort(int c) {
  if (c == 0) return "DESPEJADO";
  if (c <= 2) return "POCO NUBOSO";
  if (c == 3) return "NUBLADO";
  if (c <= 48) return "NIEBLA";
  if (c <= 57) return "LLOVIZNA";
  if (c <= 67) return "LLUVIA";
  if (c <= 77) return "NIEVE";
  if (c <= 82) return "CHUBASCOS";
  if (c <= 86) return "NEVADAS";
  return "TORMENTA";
}

static void drawClock(lv_event_t* e) {
  static const char* DAYS[] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
  static const char* MON[] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
  Hud h;
  hudBegin(h, e);
  for (int i = 0; i < 60; i++) {
    float a = i * 6 * DEG_TO_RAD;
    int r2 = i % 5 ? 104 : 96;
    hudLine(h, 120 + 110 * sinf(a), 120 - 110 * cosf(a), 120 + r2 * sinf(a), 120 - r2 * cosf(a),
            i % 5 ? G_DIM : G_MID, i % 5 ? 1 : 2);
  }
  hudRing(h, 120, 120, 40, 1, G_FAINT);
  hudRing(h, 120, 120, 76, 1, G_FAINT);

  struct tm ti;
  bool ok = timeValid(ti);
  struct timeval now;
  gettimeofday(&now, NULL);
  float sec = ok ? ti.tm_sec + now.tv_usec / 1e6f : 0;
  float sa = sec * 6;  // seconds = radar sweep
  for (int k = 0; k < 8; k++) {
    float a1 = sa - k * 4, a0 = a1 - 4, f = 1.0f - k / 8.0f;
    hudArc(h, 120, 120, 106, 106, (int)(a0 - 90), (int)(a1 - 90), G_BRIGHT, (lv_opa_t)(80 * f * f));
  }
  float ar = sa * DEG_TO_RAD;
  hudLine(h, 120, 120, 120 + 106 * sinf(ar), 120 - 106 * cosf(ar), G_BRIGHT, 2);
  if (ok) {
    float hm = ((ti.tm_hour % 12) + ti.tm_min / 60.0f) * 30 * DEG_TO_RAD;
    float mm = (ti.tm_min + sec / 60) * 6 * DEG_TO_RAD;
    hudLine(h, 120, 120, 120 + 46 * sinf(hm), 120 - 46 * cosf(hm), G_BRIGHT, 4);
    hudLine(h, 120, 120, 120 + 74 * sinf(mm), 120 - 74 * cosf(mm), G_MID, 3);
  }
  hudCircle(h, 120, 120, 3, G_BRIGHT);

  hudRect(h, 62, 146, 116, 54, C_BG, 0, 6, LV_OPA_80);
  char b[40];
  if (ok) snprintf(b, sizeof b, "%02d:%02d", ti.tm_hour, ti.tm_min);
  else strcpy(b, "--:--");
  hudText(h, b, 120, 162, &font_num, G_BRIGHT);
  if (ok) {
    snprintf(b, sizeof b, "%s %d %s", DAYS[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon]);
    hudText(h, b, 120, 181, &font_tiny, G_MID);
  }
  if (weather.valid) {
    snprintf(b, sizeof b, "%d° %s", (int)lroundf(weather.temp), wxShort(weather.code));
    hudText(h, b, 120, 192, &font_tiny, G_MID);
  }
  if (!WiFi.isConnected()) hudText(h, "SIN WIFI", 120, 52, &font_cap, C_WARN);
  else if (weather.city[0]) {
    String c = weather.city;
    c.toUpperCase();
    hudText(h, c.c_str(), 120, 52, &font_tiny, G_DIM);
  }
  hudScanlines(h, LV_OPA_40);
}

static lv_obj_t* hudView(lv_obj_t* parent, lv_event_cb_t draw) {
  lv_obj_t* o = uiBox(parent);
  lv_obj_set_size(o, 240, 240);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN, NULL);
  return o;
}

static void buildClock(lv_obj_t* t) { clockView = hudView(t, drawClock); }

static void tickClock() {}

// ================================================================= monitor (style A: combat HUD)
static lv_obj_t* monView;
static float shownCpu = 0, shownGpu = 0, shownRam = 0, shownTemp = 0;

static void tape(Hud& h, int x, float v, const char* lab, int side, bool on) {
  if (hudClipPush(h, x - 22, 62, 44, 118)) {
    for (int k = (int)floorf((v - 30) / 5) * 5; k <= v + 30; k += 5) {
      if (k < 0 || k > 100) continue;
      float y = 121 - (k - v) * 2.2f;
      bool big = k % 10 == 0;
      hudLine(h, x + side * 14, y, x + side * (big ? 4 : 9), y, G_BRIGHT, 1);
      if (big) {
        char b[6];
        snprintf(b, sizeof b, "%d", k);
        hudText(h, b, x - side * 8, (int)y, &font_tiny, G_MID);
      }
    }
  }
  hudClipPop(h);
  hudRect(h, x - 20, 112, 40, 18, C_BG);
  hudRect(h, x - 20, 112, 40, 18, v >= 90 ? C_HOT : v >= 75 ? C_WARN : G_BRIGHT, 2);
  char b[6];
  if (on) snprintf(b, sizeof b, "%d", (int)lroundf(v));
  else strcpy(b, "--");
  hudText(h, b, x, 121, &font_body, v >= 90 ? C_HOT : v >= 75 ? C_WARN : G_BRIGHT);
  hudText(h, lab, x, 52, &font_cap, G_BRIGHT);
}

static void drawMonitor(lv_event_t* e) {
  Hud h;
  hudBegin(h, e);
  bool on = pcOnline();
  tape(h, 52, shownCpu, "CPU", 1, on);
  tape(h, 188, shownGpu, "GPU", -1, on);
  // velocity vector rides with the CPU load
  float vy = 108 - (shownCpu - 50) * 0.5f;
  hudRing(h, 120, (int)vy, 6, 2, G_BRIGHT);
  hudLine(h, 104, vy, 113, vy, G_BRIGHT, 2);
  hudLine(h, 127, vy, 136, vy, G_BRIGHT, 2);
  hudLine(h, 120, vy - 6, 120, vy - 13, G_BRIGHT, 2);
  char b[32];
  String host = on ? String(pc.host) : String("RNXPC");
  host.toUpperCase();
  hudText(h, on ? host.c_str() : (WiFi.isConnected() ? "ESPERANDO PC" : "SIN WIFI"), 120, 38, &font_cap,
          on ? G_BRIGHT : C_WARN);
  if (on) {
    snprintf(b, sizeof b, "RAM %d%%", (int)lroundf(shownRam));
    hudText(h, b, 120, 152, &font_body, G_BRIGHT);
    snprintf(b, sizeof b, "TMP %d°C", (int)lroundf(shownTemp));
    hudText(h, b, 120, 172, &font_cap, shownTemp > 80 ? C_WARN : G_MID);
    snprintf(b, sizeof b, "VRAM %d%%", (int)lroundf(pc.vram));
    hudText(h, b, 120, 188, &font_tiny, G_DIM);
  }
  if (pc.gpuName[0]) hudText(h, pc.gpuName, 120, 204, &font_tiny, G_DIM);
}

static void buildMonitor(lv_obj_t* t) { monView = hudView(t, drawMonitor); }

static void fastMonitor() {
  bool on = pcOnline();
  shownCpu += ((on ? pc.cpu : 0) - shownCpu) * 0.2f;
  shownGpu += ((on ? pc.gpu : 0) - shownGpu) * 0.2f;
  shownRam += ((on ? pc.ram : 0) - shownRam) * 0.2f;
  shownTemp += ((on ? pc.gpuTemp : 0) - shownTemp) * 0.2f;
  if (lv_scr_act() != lv_obj_get_screen(tv)) return;
  if (uiTile() == TILE_MON) lv_obj_invalidate(monView);
  if (uiTile() == TILE_CLOCK) lv_obj_invalidate(clockView);
}

static void tickMonitor() {}

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
  } else if (!strncmp(cmd, "anim ", 5)) {  // play eye animation N (0-based)
    forceEye = true;
    eyeShow();
    eyePlay(atoi(cmd + 5));
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
