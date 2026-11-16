// Plane radar: an old-school phosphor radar of the traffic around your home,
// plus a full-screen "spotlight" alert (emergencies / notable aircraft) with a
// world map + route rendered by the PC agent behind the radar sweep.
#include <ArduinoJson.h>
#include "app.h"
#include "theme.h"

#define G_BRIGHT lv_color_hex(0x39FF88)
#define G_MID lv_color_hex(0x22B86C)
#define G_DIM lv_color_hex(0x13633C)
#define G_FAINT lv_color_hex(0x0A3A23)
#define RADAR_R 112
#define MAX_PLANES 10
#define WEDGE 9
#define SWEEP_MS 3200
#define MAP_BYTES (16 * 4 + 120 * 240)  // LVGL INDEXED_4BIT: palette + pixels

// ================================================================= data
struct Plane {
  char cs[10], type[8], route[48], model[32];
  float x, y;  // km east / north of home
  int alt, spd, hdg;
  float glow;
};
static Plane planes[MAX_PLANES];
static int nPlanes = 0, selected = 0;
static float rangeKm = 15;
static bool radarOn = false;
static uint32_t radarRx = 0;
static char selCs[10] = "";

struct Alert {
  uint16_t id = 0;
  char label[32] = "", cs[12] = "", model[40] = "", route[56] = "", data[48] = "";
  int px = 120, py = 120, ox = -1, oy = -1, dx = -1, dy = -1, secs = 60;
  char o[6] = "", d[6] = "";
  bool emergency = false;
  uint32_t at = 0;
};
static Alert pending, shown;
// LVGL reads the palette as 32-bit words and the C3 (RISC-V) faults on misaligned loads
alignas(4) static uint8_t mapBuf[MAP_BYTES];
static uint32_t mapMask = 0;
static uint16_t mapRxId = 0, mapReadyId = 0;
static bool alertOn = false;
static uint32_t alertEnd = 0;

// ================================================================= sweep
struct Sweep {
  lv_obj_t* wedge[WEDGE];
  lv_obj_t* line;
  lv_point_t pts[2];
  float bearing = 0;
};
static Sweep radarSweep, alertSweep;

static int norm360(float a) {
  int v = (int)a % 360;
  return v < 0 ? v + 360 : v;
}

static void sweepBuild(Sweep& s, lv_obj_t* p, lv_opa_t maxOpa) {
  for (int i = 0; i < WEDGE; i++) {
    lv_obj_t* a = lv_arc_create(p);
    lv_obj_remove_style_all(a);
    lv_obj_set_size(a, RADAR_R * 2, RADAR_R * 2);
    lv_obj_center(a);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, RADAR_R, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, G_BRIGHT, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
    float f = 1.0f - (float)i / WEDGE;
    lv_obj_set_style_arc_opa(a, (lv_opa_t)(maxOpa * f * f), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    s.wedge[i] = a;
  }
  s.line = lv_line_create(p);
  lv_obj_remove_style_all(s.line);
  lv_obj_set_style_line_color(s.line, G_BRIGHT, 0);
  lv_obj_set_style_line_width(s.line, 2, 0);
  lv_obj_set_style_line_rounded(s.line, true, 0);
  lv_obj_clear_flag(s.line, LV_OBJ_FLAG_CLICKABLE);
}

static void sweepSet(Sweep& s) {
  for (int i = 0; i < WEDGE; i++) {
    float a1 = s.bearing - i * 3.5f, a0 = a1 - 3.5f;  // bearing 0 = north; LVGL 0 = east
    lv_arc_set_bg_angles(s.wedge[i], norm360(a0 - 90), norm360(a1 - 90));
  }
  float r = s.bearing * DEG_TO_RAD;
  s.pts[0] = {120, 120};
  s.pts[1] = {(lv_coord_t)(120 + RADAR_R * sinf(r)), (lv_coord_t)(120 - RADAR_R * cosf(r))};
  lv_line_set_points(s.line, s.pts, 2);
}

// ================================================================= radar tile
static lv_obj_t *blip[MAX_PLANES], *tail[MAX_PLANES], *tag[MAX_PLANES], *selRing;
static lv_point_t tailPts[MAX_PLANES][2];
static lv_obj_t *pnl1, *pnl2, *pnl3, *pnl4, *ringLbl[3];

static lv_obj_t* circle(lv_obj_t* p, int d, lv_color_t border, int bw, lv_color_t fill, lv_opa_t fillOpa) {
  lv_obj_t* c = uiBox(p);
  lv_obj_set_size(c, d, d);
  lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(c, bw, 0);
  lv_obj_set_style_border_color(c, border, 0);
  lv_obj_set_style_bg_color(c, fill, 0);
  lv_obj_set_style_bg_opa(c, fillOpa, 0);
  return c;
}

static lv_obj_t* hline(lv_obj_t* p, lv_point_t* pts, lv_color_t c) {
  lv_obj_t* l = lv_line_create(p);
  lv_obj_remove_style_all(l);
  lv_line_set_points(l, pts, 2);
  lv_obj_set_style_line_color(l, c, 0);
  lv_obj_set_style_line_width(l, 1, 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}

static void tapCb(lv_event_t*) {
  if (nPlanes > 1) {
    selected = (selected + 1) % nPlanes;
    strlcpy(selCs, planes[selected].cs, sizeof selCs);
  }
}

static lv_obj_t* panel(lv_obj_t* p, int y, int h = 56) {
  lv_obj_t* b = uiBox(p);
  lv_obj_set_size(b, 164, h);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_bg_color(b, C_BG, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_60, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, G_FAINT, 0);
  lv_obj_align(b, LV_ALIGN_CENTER, 0, y);
  return b;
}

static lv_obj_t* panelLine(lv_obj_t* b, const lv_font_t* f, lv_color_t c, int y) {
  lv_obj_t* l = uiLabel(b, f, c, "");
  lv_obj_set_size(l, 150, lv_font_get_line_height(f));  // fixed height -> "..." instead of wrapping
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  return l;
}

void radarBuild(lv_obj_t* t) {
  static lv_point_t hx[2] = {{120 - RADAR_R, 120}, {120 + RADAR_R, 120}};
  static lv_point_t vy[2] = {{120, 120 - RADAR_R}, {120, 120 + RADAR_R}};
  hline(t, hx, G_FAINT);
  hline(t, vy, G_FAINT);
  for (int i = 1; i <= 3; i++) {
    lv_obj_t* c = circle(t, RADAR_R * 2 * i / 3, i == 3 ? G_DIM : G_FAINT, 1, C_BG, LV_OPA_TRANSP);
    lv_obj_center(c);
    ringLbl[i - 1] = uiLabel(t, &font_cap, G_DIM, "");
  }
  sweepBuild(radarSweep, t, 110);

  lv_obj_t* n = uiLabel(t, &font_cap, G_MID, "N");
  lv_obj_align(n, LV_ALIGN_CENTER, 0, -RADAR_R + 12);
  lv_obj_t* home = circle(t, 6, G_BRIGHT, 0, G_BRIGHT, LV_OPA_COVER);
  lv_obj_center(home);

  // callsign / model / route + distance / altitude + speed
  lv_obj_t* b = panel(t, 60, 74);
  pnl1 = panelLine(b, &font_body, G_BRIGHT, 3);
  pnl4 = panelLine(b, &font_small, G_MID, 22);
  pnl2 = panelLine(b, &font_small, G_MID, 38);
  lv_label_set_long_mode(pnl2, LV_LABEL_LONG_SCROLL_CIRCULAR);  // long ICAO routes scroll
  pnl3 = panelLine(b, &font_small, G_DIM, 54);

  for (int i = 0; i < MAX_PLANES; i++) {
    tail[i] = lv_line_create(t);
    lv_obj_remove_style_all(tail[i]);
    lv_obj_set_style_line_color(tail[i], G_BRIGHT, 0);
    lv_obj_set_style_line_width(tail[i], 2, 0);
    blip[i] = circle(t, 7, G_BRIGHT, 0, G_BRIGHT, LV_OPA_COVER);
    tag[i] = uiLabel(t, &font_cap, G_BRIGHT, "");
    for (lv_obj_t* o : {tail[i], blip[i], tag[i]}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  selRing = circle(t, 18, G_BRIGHT, 1, C_BG, LV_OPA_TRANSP);
  lv_obj_add_flag(selRing, LV_OBJ_FLAG_HIDDEN);



  lv_obj_add_event_cb(t, tapCb, LV_EVENT_SHORT_CLICKED, NULL);
}

static void fmtThousands(char* out, size_t n, int v) {
  if (v >= 1000) snprintf(out, n, "%d.%03d", v / 1000, v % 1000);
  else snprintf(out, n, "%d", v);
}

void radarOnJson(JsonDocument& d) {
  radarOn = (d["on"] | 1) == 1;
  radarRx = millis();
  rangeKm = d["r"] | 15.0f;
  bool autoShow = d["auto"] | false;
  bool newcomer = false;
  static Plane old[MAX_PLANES];
  int nOld = nPlanes;
  memcpy(old, planes, sizeof old);
  JsonArray arr = d["ac"].as<JsonArray>();
  int n = 0;
  for (JsonObject a : arr) {
    if (n >= MAX_PLANES) break;
    Plane& p = planes[n];
    const char* cs = a["c"] | "?";
    float glow = -1;
    for (int j = 0; j < nOld; j++)
      if (!strcmp(old[j].cs, cs)) glow = old[j].glow;
    if (glow < 0) newcomer = true;
    strlcpy(p.cs, cs, sizeof p.cs);
    strlcpy(p.type, a["ty"] | "", sizeof p.type);
    strlcpy(p.route, a["rt"] | "", sizeof p.route);
    strlcpy(p.model, a["md"] | "", sizeof p.model);
    p.x = a["x"] | 0.0f;
    p.y = a["y"] | 0.0f;
    p.alt = a["al"] | 0;
    p.spd = a["s"] | 0;
    p.hdg = a["h"] | 0;
    p.glow = glow < 0 ? 0 : glow;  // newcomers appear when the sweep finds them
    n++;
  }
  nPlanes = n;
  selected = 0;
  for (int i = 0; i < nPlanes; i++)
    if (!strcmp(planes[i].cs, selCs)) selected = i;
  if (nPlanes) strlcpy(selCs, planes[selected].cs, sizeof selCs);

  if (autoShow && newcomer && nPlanes && !alertOn && !eyeActive() && !media.playing &&
      millis() - lastTouchMs() > 15000 && uiTile() != TILE_RADAR)
    uiGoTile(TILE_RADAR);
}

void radarFast(uint32_t dt) {
  radarSweep.bearing = fmodf(radarSweep.bearing + 360.0f * dt / SWEEP_MS, 360);
  alertSweep.bearing = radarSweep.bearing;
  if (alertOn) sweepSet(alertSweep);
  if (uiTile() != TILE_RADAR || lv_scr_act() == NULL) return;
  sweepSet(radarSweep);

  float scale = RADAR_R / rangeKm;
  for (int i = 0; i < MAX_PLANES; i++) {
    if (i >= nPlanes) {
      for (lv_obj_t* o : {tail[i], blip[i], tag[i]}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    Plane& p = planes[i];
    // dead-reckoning between 5 s updates
    float h = p.hdg * DEG_TO_RAD, km = p.spd / 3600.0f * dt / 1000.0f;
    p.x += km * sinf(h);
    p.y += km * cosf(h);
    float dist = sqrtf(p.x * p.x + p.y * p.y);
    bool visible = dist <= rangeKm * 1.02f;
    // light up when the sweep passes, then fade like phosphor
    float pb = atan2f(p.x, p.y) * RAD_TO_DEG;
    float diff = fmodf(radarSweep.bearing - pb + 720.0f, 360.0f);
    p.glow = diff < 10 ? 255 : max(55.0f, p.glow * 0.985f);
    int cx = 120 + (int)(p.x * scale), cy = 120 - (int)(p.y * scale);
    lv_obj_set_pos(blip[i], cx - 3, cy - 3);
    tailPts[i][0] = {(lv_coord_t)cx, (lv_coord_t)cy};
    tailPts[i][1] = {(lv_coord_t)(cx - 11 * sinf(h)), (lv_coord_t)(cy + 11 * cosf(h))};
    lv_line_set_points(tail[i], tailPts[i], 2);
    lv_obj_set_pos(tag[i], cx + 6, cy - 15);
    lv_label_set_text_static(tag[i], p.cs);
    lv_opa_t o = (lv_opa_t)p.glow;
    for (lv_obj_t* obj : {tail[i], blip[i], tag[i]}) {
      lv_obj_set_style_opa(obj, i == selected ? max<lv_opa_t>(o, 160) : o, 0);
      if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    if (i == selected) lv_obj_set_pos(selRing, cx - 9, cy - 9);
  }
  if (nPlanes) lv_obj_clear_flag(selRing, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(selRing, LV_OBJ_FLAG_HIDDEN);
}

void radarTick() {
  char b[64];
  for (int i = 0; i < 3; i++) {
    int km = (int)lroundf(rangeKm * (i + 1) / 3);
    snprintf(b, sizeof b, i == 2 ? "%d KM" : "%d", km);
    if (strcmp(lv_label_get_text(ringLbl[i]), b)) {
      lv_label_set_text(ringLbl[i], b);
      // sit just inside each ring, under the east axis
      lv_obj_update_layout(ringLbl[i]);
      int r = RADAR_R * (i + 1) / 3;
      lv_obj_set_pos(ringLbl[i], 120 + r - lv_obj_get_width(ringLbl[i]) - 4, 124);
    }
  }
  bool fresh = radarRx && millis() - radarRx < 20000;
  if (!pcOnline() || !fresh || !radarOn) {
    if (!pcOnline()) {
      uiSetText(pnl1, "SIN DATOS");
      uiSetText(pnl2, "PC desconectado");
    } else if (!fresh) {
      uiSetText(pnl1, "BUSCANDO...");
      uiSetText(pnl2, "Esperando al radar");
    } else {
      uiSetText(pnl1, "RADAR APAGADO");
      uiSetText(pnl2, "Actívalo en el agente");
    }
    uiSetText(pnl3, "");
    uiSetText(pnl4, "");
    if (!pcOnline() || !fresh) nPlanes = 0;
    return;
  }
  if (!nPlanes) {
    uiSetText(pnl1, "SIN TRÁFICO");
    snprintf(b, sizeof b, "Radio %d km", (int)lroundf(rangeKm));
    uiSetText(pnl2, b);
    uiSetText(pnl3, "");
    uiSetText(pnl4, "");
    return;
  }
  Plane& p = planes[selected];
  snprintf(b, sizeof b, "%s  %s", p.cs, p.type);
  uiSetText(pnl1, b);
  uiSetText(pnl4, p.model[0] ? p.model : p.type);
  float dist = sqrtf(p.x * p.x + p.y * p.y);
  uiSetText(pnl2, p.route[0] ? p.route : "Ruta desconocida");
  char alt[12];
  fmtThousands(alt, sizeof alt, p.alt);
  snprintf(b, sizeof b, "%.1fkm · %sm · %dkm/h", dist, alt, p.spd);
  uiSetText(pnl3, b);
}

// ================================================================= spotlight alert
static lv_obj_t *alScr, *alPrev, *alImg, *alPlane, *alPulse, *alO, *alD, *alCap, *alCs;
static lv_obj_t *alRoute, *alModel, *alData, *alArc;
static lv_img_dsc_t mapDsc;

static void setSize(void* o, int32_t v) {
  lv_obj_set_size((lv_obj_t*)o, v, v);
}
static void setOpa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, v, 0); }

static void alertClose() {
  if (!alertOn) return;
  alertOn = false;
  lv_anim_del(alPulse, NULL);
  lv_scr_load_anim(alPrev, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

static void alertTap(lv_event_t*) { alertClose(); }

void alertBuild() {
  alScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(alScr, C_BG, 0);
  lv_obj_clear_flag(alScr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(alScr, alertTap, LV_EVENT_SHORT_CLICKED, NULL);

  alImg = lv_img_create(alScr);
  lv_obj_set_pos(alImg, 0, 0);
  mapDsc.header.always_zero = 0;
  mapDsc.header.w = 240;
  mapDsc.header.h = 240;
  mapDsc.header.cf = LV_IMG_CF_INDEXED_4BIT;
  mapDsc.data_size = MAP_BYTES;
  mapDsc.data = mapBuf;

  sweepBuild(alertSweep, alScr, 70);

  alO = uiLabel(alScr, &font_cap, G_BRIGHT, "");
  alD = uiLabel(alScr, &font_cap, G_BRIGHT, "");
  alPulse = circle(alScr, 10, G_BRIGHT, 2, C_BG, LV_OPA_TRANSP);
  alPlane = circle(alScr, 9, C_BG, 2, lv_color_white(), LV_OPA_COVER);

  alArc = uiArc(alScr, 236, 3, C_WARN, 270, 360);
  lv_obj_set_style_arc_color(alArc, C_BG, LV_PART_MAIN);

  // keep the caption well inside the countdown ring (the circle narrows fast up there)
  alCap = uiCaption(alScr, "");
  lv_obj_set_style_text_letter_space(alCap, 1, 0);
  lv_obj_align(alCap, LV_ALIGN_CENTER, 0, -84);
  alCs = uiLabel(alScr, &font_value, C_TXT, "");
  lv_obj_align(alCs, LV_ALIGN_CENTER, 0, -64);
  lv_obj_set_style_bg_color(alCs, C_BG, 0);
  lv_obj_set_style_bg_opa(alCs, LV_OPA_60, 0);
  lv_obj_set_style_pad_hor(alCs, 6, 0);
  lv_obj_set_style_radius(alCs, 6, 0);

  lv_obj_t* b = panel(alScr, 72);
  alRoute = panelLine(b, &font_body, C_TXT, 3);
  lv_label_set_long_mode(alRoute, LV_LABEL_LONG_SCROLL_CIRCULAR);
  alModel = panelLine(b, &font_small, C_TXT2, 22);
  alData = panelLine(b, &font_small, C_TXT3, 38);
}

static void place(lv_obj_t* l, int x, int y, const char* txt) {
  if (x < 0) {
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_label_set_text(l, txt);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(l);
  lv_obj_set_pos(l, constrain(x - lv_obj_get_width(l) / 2, 20, 200), constrain(y + 6, 20, 210));
}

void alertDebug();

static void alertShow() {
  shown = pending;
  pending.id = 0;
  bool hasMap = mapReadyId == shown.id;
  if (hasMap) {
    lv_img_cache_invalidate_src(&mapDsc);
    lv_img_set_src(alImg, &mapDsc);
    lv_obj_clear_flag(alImg, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(alImg, LV_OBJ_FLAG_HIDDEN);
  }
  lv_color_t c = shown.emergency ? C_HOT : C_WARN;
  lv_obj_set_style_text_color(alCap, c, 0);
  lv_obj_set_style_arc_color(alArc, c, LV_PART_INDICATOR);
  lv_label_set_text(alCap, shown.label);
  lv_label_set_text(alCs, shown.cs);
  lv_label_set_text(alRoute, shown.route[0] ? shown.route : "Ruta desconocida");
  lv_label_set_text(alModel, shown.model);
  lv_label_set_text(alData, shown.data);
  place(alO, shown.ox, shown.oy, shown.o);
  place(alD, shown.dx, shown.dy, shown.d);
  lv_obj_set_pos(alPlane, shown.px - 4, shown.py - 4);
  lv_obj_align(alPulse, LV_ALIGN_TOP_LEFT, shown.px - 5, shown.py - 5);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, alPulse);
  lv_anim_set_exec_cb(&a, [](void* o, int32_t v) {
    lv_obj_t* obj = (lv_obj_t*)o;
    lv_obj_set_size(obj, v, v);
    lv_obj_set_pos(obj, shown.px - v / 2, shown.py - v / 2);
    lv_obj_set_style_opa(obj, (lv_opa_t)(255 - (v - 10) * 255 / 30), 0);
  });
  lv_anim_set_values(&a, 10, 40);
  lv_anim_set_time(&a, 1400);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);

  alertOn = true;
  alertEnd = millis() + shown.secs * 1000UL;
  alPrev = lv_scr_act();
  lv_scr_load_anim(alScr, LV_SCR_LOAD_ANIM_FADE_ON, 400, 0, false);
  hwPoke();
}

void alertOnJson(JsonDocument& d) {
  uint16_t id = d["id"] | 0;
  if (!id || id == shown.id || id == pending.id) return;
  Alert a;
  a.id = id;
  strlcpy(a.label, d["lbl"] | "", sizeof a.label);
  strlcpy(a.cs, d["c"] | "", sizeof a.cs);
  strlcpy(a.model, d["m"] | "", sizeof a.model);
  strlcpy(a.route, d["rt"] | "", sizeof a.route);
  strlcpy(a.data, d["dt"] | "", sizeof a.data);
  a.emergency = d["em"] | false;
  a.secs = d["sec"] | 60;
  a.px = d["p"][0] | 120;
  a.py = d["p"][1] | 120;
  a.ox = d["o"][0] | -1;
  a.oy = d["o"][1] | -1;
  strlcpy(a.o, d["o"][2] | "", sizeof a.o);
  a.dx = d["d"][0] | -1;
  a.dy = d["d"][1] | -1;
  strlcpy(a.d, d["d"][2] | "", sizeof a.d);
  a.at = millis();
  pending = a;
}

// 'M' 'P' id_lo id_hi chunk_idx chunk_count payload
void alertOnMapChunk(const uint8_t* p, int n) {
  uint16_t id = p[2] | (p[3] << 8);
  uint8_t idx = p[4], total = p[5];
  int len = n - 6;
  if (alertOn || total == 0 || total > 31 || idx >= total) return;  // never write a map on screen
  if (id != mapRxId) {
    mapRxId = id;
    mapMask = 0;
    mapReadyId = 0;
  }
  size_t off = (size_t)idx * ART_CHUNK;
  if (len <= 0 || off + len > sizeof mapBuf) return;
  memcpy(mapBuf + off, p + 6, len);
  mapMask |= 1UL << idx;
  if (mapMask == (1UL << total) - 1) mapReadyId = id;
}

void alertTick() {
  if (alertOn) {
    long left = (long)(alertEnd - millis());
    lv_arc_set_value(alArc, left > 0 ? (int)(left * 1000 / (shown.secs * 1000L)) : 0);
    if (left <= 0) alertClose();
    return;
  }
  // wait (briefly) for the map to arrive before showing
  if (pending.id && (mapReadyId == pending.id || millis() - pending.at > 6000)) alertShow();
}

bool alertActive() { return alertOn; }

void alertDebugMap() {  // serial "alertmap": synthetic map, no network involved
  for (int i = 0; i < 16; i++) {
    mapBuf[i * 4 + 0] = i * 12;  // B
    mapBuf[i * 4 + 1] = i * 16;  // G
    mapBuf[i * 4 + 2] = 0;       // R
    mapBuf[i * 4 + 3] = 255;
  }
  for (int i = 64; i < MAP_BYTES; i++) mapBuf[i] = (((i - 64) / 120 / 15) & 15) * 0x11;
  alertDebug();
  mapReadyId = pending.id;
  Serial.printf("alertmap: id %u, heap %u\n", pending.id, ESP.getFreeHeap());
}

void alertDebug() {  // serial "alert": demo without map
  JsonDocument d;
  deserializeJson(d,
                  "{\"id\":65000,\"lbl\":\"EMERGENCIA · 7700\",\"c\":\"IBE6253\",\"m\":\"Airbus "
                  "A350-900\",\"rt\":\"MAD → MEX\",\"dt\":\"10.360 m · 870 km/h\",\"em\":true,"
                  "\"sec\":20,\"p\":[132,118],\"o\":[70,96,\"MAD\"],\"d\":[196,150,\"MEX\"]}");
  shown.id = 0;
  alertOnJson(d);
  pending.at = 0;
}
