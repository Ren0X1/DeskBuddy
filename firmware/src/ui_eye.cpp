// Idle companion: HUD-style eyes driven by the keyframe table in eye_anims.h
// (generated from tools/eyes.py). Tap = something happy. Long-press = dismiss
// until you use the PC again.
//
// Each eye is drawn as "visible shape" = rounded rect, minus the eyelid (a line
// mask, slanted for angry/sad) and minus the cheek (an inverted circle mask for
// ^ ^ eyes). The outline is that same shape filled bright, then refilled dark
// 3 px inset, so the contour always follows the lids.
#include "app.h"
#include "eye_anims.h"
#include "hud.h"
#include "theme.h"

struct EyeP {
  float x, y, w, h, r, lt, la, lb, s;
};
struct EyeS {
  EyeP L, R;
  float dx, dy, rot;
};
static const EyeS BASE_S = {{-42, 0, 54, 74, 20, 0, 0, 0, 1}, {42, 0, 54, 74, 20, 0, 0, 0, 1}, 0, 0, 0};
static const EyeS SLEEP_S = {{-42, 0, 54, 6, 20, 0, 0, 0, 1}, {42, 0, 54, 6, 20, 0, 0, 0, 1}, 0, 14, 0};

static lv_obj_t *scr, *view;  // scr = full-screen layer on lv_layer_top(), not a separate screen
static bool active = false, sleepy = false, dismissed = false, cameo = false, sad = false, transition = false,
            sleepPending = false;
static int queue[4], qn = 0, idxSad = -1, idxCry = -1;
static int cur = -1;
static uint32_t t0 = 0, restUntil = 0, animTotal[EYE_ANIM_COUNT];
static EyeS st = BASE_S;
static int calm[12], nCalm = 0, idxSleep = -1, idxBoot = -1;
static bool excluded[EYE_ANIM_COUNT];

// ---------------------------------------------------------------- keyframes
static void applyKv(EyeS& s, uint8_t f, float v) {
  if (f < 3) {
    (&s.dx)[f] = v;
    return;
  }
  int i = f - 3, group = i / 9, field = i % 9;
  if (group != 2) (&s.L.x)[field] = v;  // 0 = both, 1 = left
  if (group != 1) (&s.R.x)[field] = v;  // 0 = both, 2 = right
}

static EyeS target(int stepIdx) {
  EyeS s = BASE_S;
  const EyeStep& stp = EYE_STEPS[stepIdx];
  for (int i = 0; i < stp.n; i++) applyKv(s, EYE_KV[stp.kv + i].f, EYE_KV[stp.kv + i].v);
  return s;
}

static EyeS lerpS(const EyeS& a, const EyeS& b, float f) {
  EyeS o;
  const float *pa = (const float*)&a, *pb = (const float*)&b;
  float* po = (float*)&o;
  for (size_t i = 0; i < sizeof(EyeS) / sizeof(float); i++) po[i] = pa[i] + (pb[i] - pa[i]) * f;
  return o;
}

static float ease(float f) { return f < .5f ? 2 * f * f : 1 - powf(-2 * f + 2, 2) / 2; }

static EyeS sample(int a, uint32_t tau) {
  const EyeAnim& an = EYE_ANIMS[a];
  uint32_t acc = 0;
  EyeS from = BASE_S, to = BASE_S;
  for (int i = 0; i < an.n; i++) {
    uint16_t ms = EYE_STEPS[an.step + i].ms;
    to = target(an.step + i);
    if (tau <= acc + ms) return lerpS(from, to, ease(ms ? (float)(tau - acc) / ms : 1));
    acc += ms;
    from = to;
  }
  return to;
}

static int findAnim(const char* name) {
  for (int i = 0; i < EYE_ANIM_COUNT; i++)
    if (!strcmp(EYE_ANIMS[i].name, name)) return i;
  return -1;
}

// ---------------------------------------------------------------- drawing
static void shape(Hud& h, const EyeP& e, int side, float cx, float cy, float W, float H, int in,
                  lv_color_t col) {
  float w = W - 2 * in, hh = H - 2 * in;
  if (w < 1 || hh < 1) return;
  lv_draw_mask_line_param_t lp;
  lv_draw_mask_radius_param_t rp;
  int16_t id1 = LV_MASK_ID_INV, id2 = LV_MASK_ID_INV;
  if (e.lt > 0.001f || fabsf(e.la) > 0.1f) {  // eyelid
    float k = tanf(e.la * DEG_TO_RAD) * (side == 0 ? 1 : -1);
    float y0 = cy - H / 2 + e.lt * H + in;
    lv_draw_mask_line_points_init(&lp, h.ox + cx - 100, h.oy + y0 - 100 * k, h.ox + cx + 100,
                                  h.oy + y0 + 100 * k, LV_DRAW_MASK_LINE_SIDE_BOTTOM);
    id1 = lv_draw_mask_add(&lp, NULL);
  }
  if (e.lb > 0.001f) {  // cheek -> ^ ^
    float R0 = H * 0.9f, ccy = cy + H / 2 + R0 - e.lb * H * 0.9f, rr = R0 + in;
    lv_area_t ca = {(lv_coord_t)(h.ox + cx - rr), (lv_coord_t)(h.oy + ccy - rr), (lv_coord_t)(h.ox + cx + rr),
                    (lv_coord_t)(h.oy + ccy + rr)};
    lv_draw_mask_radius_init(&rp, &ca, LV_RADIUS_CIRCLE, true);
    id2 = lv_draw_mask_add(&rp, NULL);
  }
  int rad = (int)max(0.0f, min(e.r * e.s - in, min(w, hh) / 2));
  hudRect(h, (int)(cx - w / 2), (int)(cy - hh / 2), (int)w, (int)hh, col, 0, rad);
  if (id1 != LV_MASK_ID_INV) {
    lv_draw_mask_remove_id(id1);
    lv_draw_mask_free_param(&lp);
  }
  if (id2 != LV_MASK_ID_INV) {
    lv_draw_mask_remove_id(id2);
    lv_draw_mask_free_param(&rp);
  }
}

static void drawEye(Hud& h, const EyeP& e, int side, const EyeS& s) {
  if (e.s < 0.02f) return;
  float W = e.w * e.s, H = max(1.0f, e.h * e.s);
  float rot = s.rot * DEG_TO_RAD;  // tilt: move the eyes, keep the shapes upright
  float cx = 120 + s.dx + e.x * cosf(rot), cy = 120 + s.dy + e.y + e.x * sinf(rot);
  shape(h, e, side, cx, cy, W, H, 0, G_BRIGHT);
  shape(h, e, side, cx, cy, W, H, 3, G_FILL);
}

static void heart(Hud& h, float x, float y, float s) {
  hudCircle(h, x - s * .5f, y - s * .25f, s * .55f, G_BRIGHT);
  hudCircle(h, x + s * .5f, y - s * .25f, s * .55f, G_BRIGHT);
  lv_point_t p[3] = {{(lv_coord_t)(x - s * 1.03f), (lv_coord_t)(y - s * .05f)},
                     {(lv_coord_t)(x + s * 1.03f), (lv_coord_t)(y - s * .05f)},
                     {(lv_coord_t)x, (lv_coord_t)(y + s * 1.05f)}};
  hudPoly(h, p, 3, G_BRIGHT);
}

static void star(Hud& h, float x, float y, float r, float rot) {
  lv_point_t p[10];
  for (int i = 0; i < 10; i++) {
    float a = rot + i * PI / 5, rr = i % 2 ? r * .45f : r;
    p[i] = {(lv_coord_t)(x + rr * sinf(a)), (lv_coord_t)(y - rr * cosf(a))};
  }
  // LVGL polygons must be convex-ish: draw as 5 triangles from the centre
  for (int i = 0; i < 10; i++) {
    lv_point_t t[3] = {{(lv_coord_t)x, (lv_coord_t)y}, p[i], p[(i + 1) % 10]};
    hudPoly(h, t, 3, G_BRIGHT);
  }
}

static void drop(Hud& h, float x, float y) {
  hudCircle(h, x, y, 5, G_BRIGHT);
  lv_point_t p[3] = {{(lv_coord_t)(x - 5), (lv_coord_t)y}, {(lv_coord_t)x, (lv_coord_t)(y - 12)}, {(lv_coord_t)(x + 5), (lv_coord_t)y}};
  hudPoly(h, p, 3, G_BRIGHT);
}

static void brackets(Hud& h, float x, float y, float s) {
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2) {
      hudLine(h, x + sx * s, y + sy * (s - 10), x + sx * s, y + sy * s, G_BRIGHT, 2);
      hudLine(h, x + sx * s, y + sy * s, x + sx * (s - 10), y + sy * s, G_BRIGHT, 2);
    }
}

static void overlay(Hud& h, uint8_t ov, float f, const EyeS& s, uint32_t t) {
  float Lx = 120 + s.dx + s.L.x, Ly = 120 + s.dy + s.L.y, Rx = 120 + s.dx + s.R.x, Ry = 120 + s.dy + s.R.y;
  float pulse = 1 + .12f * sinf(t / 120.0f);
  char b[24];
  switch (ov) {
    case OV_HEARTS: heart(h, Lx, Ly, 22 * pulse); heart(h, Rx, Ry, 22 * pulse); break;
    case OV_STARS: star(h, Lx, Ly, 26 * pulse, t / 600.0f); star(h, Rx, Ry, 26 * pulse, -(t / 600.0f)); break;
    case OV_SPIRAL:
      for (int e = 0; e < 2; e++) {
        float x = e ? Rx : Lx, y = e ? Ry : Ly, px = x, py = y;
        for (int k = 1; k < 90; k++) {
          float a = k / 8.0f + t / 150.0f * (e ? -1 : 1), r = k * .3f;
          float nx = x + r * cosf(a), ny = y + r * sinf(a);
          hudLine(h, px, py, nx, ny, G_BRIGHT, 3);
          px = nx, py = ny;
        }
      }
      break;
    case OV_XX:
      for (int e = 0; e < 2; e++) {
        float x = e ? Rx : Lx, y = e ? Ry : Ly;
        hudLine(h, x - 18, y - 18, x + 18, y + 18, G_BRIGHT, 6);
        hudLine(h, x + 18, y - 18, x - 18, y + 18, G_BRIGHT, 6);
      }
      break;
    case OV_TEAR: drop(h, Lx - 14, Ly + 30 + fmodf(f * 260, 70)); break;
    case OV_SWEAT: drop(h, Rx + 36, Ry - 40 + f * 30); break;
    case OV_ZZZ:
      for (int i = 0; i < 3; i++) {
        float k = fmodf(f * 3 + i / 3.0f, 1);
        const lv_font_t* fn = i == 0 ? &font_cap : i == 1 ? &font_body : &font_value;
        hudText(h, "z", 168 + i * 10 + k * 10, 80 - k * 40 - i * 12, fn, G_BRIGHT, HUD_CENTER, (lv_opa_t)(255 * (1 - k)));
      }
      break;
    case OV_NOTES: case OV_NOTE1: case OV_SING:
      for (int i = 0; i < (ov == OV_NOTE1 ? 1 : 3); i++) {
        float k = fmodf(f * 2 + i / 3.0f, 1);
        hudText(h, ICON_MUSIC, 60 + i * 60, 60 - k * 40, &icons_24, G_BRIGHT, HUD_CENTER, (lv_opa_t)(255 * (1 - k)));
      }
      if (ov == OV_SING) {
        int mh = 12 + (int)(12 * fabsf(sinf(t / 140.0f)));
        hudRect(h, 110, 176 + (int)s.dy - mh / 2, 20, mh, G_BRIGHT, 3, LV_RADIUS_CIRCLE);
      }
      break;
    case OV_QUESTION: hudText(h, "?", 182, 62, &font_value, G_BRIGHT); break;
    case OV_EXCLAM: hudText(h, "!", 120, 40, &font_value, G_BRIGHT); break;
    case OV_DOTS:
      for (int i = 0; i < 3; i++) hudCircle(h, 170 + i * 12, 58 - i * 6, 4, G_BRIGHT, (int)(t / 300) % 4 > i ? LV_OPA_COVER : LV_OPA_20);
      break;
    case OV_LOCK: case OV_LOCKR: {
      float sz = 34 + 10 * (1 - min(1.0f, f * 3));
      if (ov == OV_LOCK) brackets(h, Lx, Ly, sz);
      brackets(h, Rx, Ry, sz);
      if (ov == OV_LOCK && f > .3f) hudText(h, "LOCK", 120, 186, &font_cap, G_BRIGHT);
      break;
    }
    case OV_SWEEP: {
      float a = f * 720;
      for (int k = 0; k < 8; k++) {
        float a1 = a - k * 4, fk = 1 - k / 8.0f;
        hudArc(h, 120, 120, 120, 120, (int)(a1 - 4 - 90), (int)(a1 - 90), G_BRIGHT, (lv_opa_t)(110 * fk * fk));
      }
      hudLine(h, 120, 120, 120 + 120 * sinf(a * DEG_TO_RAD), 120 - 120 * cosf(a * DEG_TO_RAD), G_BRIGHT, 2);
      break;
    }
    case OV_SCAN: { float y = 60 + 120 * (.5f + .5f * sinf(t / 260.0f)); hudLine(h, 30, y, 210, y, G_BRIGHT, 2); break; }
    case OV_GRID:
      for (int i = 20; i < 240; i += 20) {
        hudLine(h, i, 0, i, 239, G_DIM, 1, LV_OPA_50);
        hudLine(h, 0, i, 239, i, G_DIM, 1, LV_OPA_50);
      }
      hudText(h, "CAL", 120, 196, &font_cap, G_BRIGHT);
      break;
    case OV_BOOT: hudText(h, f < .5f ? "SYS BOOT" : "READY", 120, 186, &font_cap, G_BRIGHT); break;
    case OV_GLITCH:
      for (int i = 0; i < 4; i++) {
        int y = random(40, 200), x = random(20, 160);
        hudRect(h, x, y, random(20, 70), 2, G_BRIGHT, 0, 0, LV_OPA_70);
        hudRect(h, random(40, 180), random(60, 180), random(30, 80), random(2, 5), C_BG);
      }
      break;
    case OV_LOADING:
      hudArc(h, 120, 120, 34, 6, (int)(t / 6) % 360, (int)(t / 6 + 240) % 360, G_BRIGHT, LV_OPA_COVER, true);
      snprintf(b, sizeof b, "%d%%", (int)(f * 100));
      hudText(h, b, 120, 120, &font_cap, G_BRIGHT);
      break;
    case OV_BLUSH:
      for (int e = 0; e < 2; e++)
        for (int i = 0; i < 3; i++) {
          float x = (e ? Rx : Lx) - 14 + i * 10, y = (e ? Ry : Ly) + 46;
          hudLine(h, x, y, x - 6, y + 10, G_MID, 2);
        }
      break;
    case OV_CONFETTI:
      for (int i = 0; i < 26; i++) {
        int x = (i * 53 + t / 7 * (i % 3 + 1)) % 240, y = (i * 97 + t / 5 * (i % 4 + 1)) % 240;
        hudRect(h, x, y, 4, 4, i % 3 ? G_BRIGHT : G_MID);
      }
      break;
    case OV_RETINA:
      for (int e = 0; e < 2; e++) {
        float x = e ? Rx : Lx, y = e ? Ry : Ly;
        for (int r = 6; r < 26; r += 6) hudRing(h, x, y, r, 2, C_BG);
        float ly = y + sinf(t / 200.0f) * 24;
        hudLine(h, x - 30, ly, x + 30, ly, C_WARN, 2);
      }
      break;
    case OV_NV:
      for (int i = 0; i < 70; i++) hudRect(h, random(0, 238), random(0, 238), 2, 2, G_BRIGHT, 0, 0, (lv_opa_t)random(20, 110));
      break;
    case OV_LOST: if ((t / 250) % 2) hudText(h, "NO TGT", 120, 186, &font_cap, C_WARN); break;
    case OV_HAHA: hudText(h, "JA JA", 120, 188, &font_body, G_BRIGHT); break;
    case OV_RAGE:
      for (int sgn = -1; sgn <= 1; sgn += 2)
        for (int i = 0; i < 3; i++) hudLine(h, 120 + sgn * (70 + i * 8), 40 - i * 4, 120 + sgn * (80 + i * 8), 28 - i * 4, C_WARN, 3);
      break;
    case OV_ALERT:
      hudRing(h, 120, 120, 110, 4, C_WARN, (t / 180) % 2 ? LV_OPA_90 : LV_OPA_20);
      hudText(h, "!", 120, 190, &font_value, C_WARN);
      break;
    case OV_YAWN: {
      float k = min(1.0f, f * 2);
      hudRect(h, 120 - 12 * k, 180 - 16 * k, 24 * k + 1, 32 * k + 1, G_BRIGHT, 3, LV_RADIUS_CIRCLE);
      break;
    }
    case OV_PLANE: {
      float x = 40 + f * 170, y = 70 - f * 30;
      lv_point_t p[4] = {{(lv_coord_t)(x + 10), (lv_coord_t)y}, {(lv_coord_t)(x - 8), (lv_coord_t)(y - 6)},
                         {(lv_coord_t)(x - 4), (lv_coord_t)y}, {(lv_coord_t)(x - 8), (lv_coord_t)(y + 6)}};
      hudPoly(h, p, 3, G_BRIGHT);
      lv_point_t q[3] = {p[0], p[2], p[3]};
      hudPoly(h, q, 3, G_BRIGHT);
      hudLine(h, x - 30, y, x - 12, y, G_DIM, 2);
      break;
    }
    case OV_LASER:
      if ((t / 90) % 3) {
        hudLine(h, Lx, Ly, 30, 239, G_BRIGHT, 3);
        hudLine(h, Rx, Ry, 210, 239, G_BRIGHT, 3);
      }
      break;
    case OV_SONAR:
      for (int k = 0; k < 3; k++) {
        int r = (int)(t / 8 + k * 40) % 120;
        hudRing(h, 120, 120, r, 2, G_BRIGHT, (lv_opa_t)(255 * (1 - r / 120.0f)));
      }
      break;
    case OV_HYPNO:
      for (int k = 0; k < 8; k++) {
        int r = (int)(k * 14 + t / 20) % 112;
        hudRing(h, 120, 120, r, 3, k % 2 ? G_BRIGHT : G_MID, (lv_opa_t)(255 * (1 - r / 112.0f)));
      }
      break;
    case OV_COUNTDOWN: {
      int n = 3 - (int)(f * 3.2f);
      snprintf(b, sizeof b, "%d", max(1, n));
      hudText(h, n > 0 ? b : "GO", 120, 186, &font_num, G_BRIGHT);
      hudArc(h, 120, 120, 112, 4, 270, 270 + (int)(360 * (1 - f)), G_BRIGHT);
      break;
    }
    case OV_MATRIX:
      for (int c = 0; c < 10; c++) {
        int x = 16 + c * 23, head = (int)((t / (6 + c % 4)) + c * 37) % 260 - 20;
        for (int j = 0; j < 6; j++) {
          b[0] = '0' + (char)((c * 7 + j * 13 + t / 150) % 10);
          b[1] = 0;
          hudText(h, b, x, head - j * 11, &font_tiny, j ? G_MID : G_BRIGHT, HUD_CENTER, (lv_opa_t)(255 - j * 40));
        }
      }
      break;
    case OV_TELEMETRY: {
      const char* lab[4] = {"ALT", "SPD", "HDG", "SYS"};
      for (int i = 0; i < 4; i++) {
        if (i < 3) snprintf(b, sizeof b, "%s %d", lab[i], (int)(i == 0 ? 10600 + (t / 7) % 300 : i == 1 ? 480 + (t / 90) % 20 : (t / 40) % 360));
        else snprintf(b, sizeof b, "SYS OK");
        hudText(h, b, 150, 84 + i * 18, &font_tiny, G_BRIGHT, HUD_LEFT);
      }
      break;
    }
    case OV_FLASH: if (f < .15f) hudRect(h, 0, 0, 240, 240, G_BRIGHT, 0, 0, (lv_opa_t)(200 * (1 - f / .15f))); break;
    case OV_AIM: {
      float x = 40 + (Rx - 40) * min(1.0f, f * 1.6f), y = 200 + (Ry - 200) * min(1.0f, f * 1.6f);
      hudRing(h, x, y, 14, 2, C_WARN);
      hudLine(h, x - 22, y, x - 8, y, C_WARN, 2);
      hudLine(h, x + 8, y, x + 22, y, C_WARN, 2);
      hudLine(h, x, y - 22, x, y - 8, C_WARN, 2);
      hudLine(h, x, y + 8, x, y + 22, C_WARN, 2);
      break;
    }
    case OV_WAVE:
      for (int k = 0; k < 3; k++) {
        float px = 30, py = 190;
        for (int x = 34; x <= 210; x += 6) {
          float y = 190 + (8 - k * 2) * sinf(x / 14.0f + t / (120.0f + k * 40)) * (.6f + .4f * sinf(t / 300.0f));
          hudLine(h, px, py, x, y, k ? G_DIM : G_BRIGHT, 2);
          px = x, py = y;
        }
      }
      break;
    default: break;
  }
}

static void draw(lv_event_t* e) {
  Hud h;
  hudBegin(h, e);
  uint32_t now = millis();
  if (sleepy && cur < 0) {
    drawEye(h, SLEEP_S.L, 0, SLEEP_S);
    drawEye(h, SLEEP_S.R, 1, SLEEP_S);
    overlay(h, OV_ZZZ, (now % 3000) / 3000.0f, SLEEP_S, now);
    return;
  }
  drawEye(h, st.L, 0, st);
  drawEye(h, st.R, 1, st);
  if (cur >= 0 && EYE_ANIMS[cur].ov) {
    float f = min(1.0f, (float)(now - t0) / max<uint32_t>(1, animTotal[cur]));
    overlay(h, EYE_ANIMS[cur].ov, f, st, now);
  }
}

// ---------------------------------------------------------------- behaviour
static void play(int a) {
  if (a < 0 || a >= EYE_ANIM_COUNT) return;
  cur = a;
  t0 = millis();
}

static void pickNext() {
  if (random(100) < 55) play(calm[random(nCalm)]);
  else {
    int a;
    do a = random(EYE_ANIM_COUNT);
    while (excluded[a]);
    play(a);
  }
}

static void eventCb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_SHORT_CLICKED) {
    static const char* happy[] = {"Feliz", "Risa", "Celebración", "Enamorado", "Saludo", "Ojos de estrella", "Orgulloso"};
    if (sleepy) sleepy = false;
    eyeReact(happy[random(7)]);
  } else if (c == LV_EVENT_LONG_PRESSED) {
    dismissed = true;
    eyeHide();
  }
}

void eyeInit() {
  for (int i = 0; i < EYE_ANIM_COUNT; i++) {
    uint32_t tot = 0;
    for (int s = 0; s < EYE_ANIMS[i].n; s++) tot += EYE_STEPS[EYE_ANIMS[i].step + s].ms;
    animTotal[i] = tot;
  }
  static const char* calmNames[] = {"Parpadeo", "Doble parpadeo", "Parpadeo lento", "Mirar a la izquierda",
                                    "Mirar a la derecha", "Mirar arriba", "Mirar abajo", "Vistazos rápidos",
                                    "Escanear", "Curioso", "Aburrido", "Pensando"};
  for (const char* n : calmNames) {
    int a = findAnim(n);
    if (a >= 0 && nCalm < 12) calm[nCalm++] = a;
  }
  // context-only animations never play at random
  static const char* ctx[] = {"Dormir", "Despertar", "Avión a la vista", "Nueva canción", "Apagado", "Apagar", "Arranque", "K.O."};
  for (const char* n : ctx) {
    int a = findAnim(n);
    if (a >= 0) excluded[a] = true;
  }
  idxSleep = findAnim("Dormir");
  idxSad = findAnim("Triste");
  idxCry = findAnim("Llorar");
  idxBoot = findAnim("Arranque");

  // Eyes live on the top layer and fade in/out on their own: no screen loads, so they can
  // never race with another screen transition, and they simply cover whatever is below.
  scr = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(scr);
  lv_obj_set_size(scr, 240, 240);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(scr);  // below the OTA / Wi-Fi overlays
  lv_obj_add_event_cb(scr, eventCb, LV_EVENT_SHORT_CLICKED, NULL);
  lv_obj_add_event_cb(scr, eventCb, LV_EVENT_LONG_PRESSED, NULL);
  view = uiBox(scr);
  lv_obj_set_size(view, 240, 240);
  lv_obj_add_event_cb(view, draw, LV_EVENT_DRAW_MAIN, NULL);
}

static void enqueue(const char* name) {
  int a = findAnim(name);
  if (a >= 0 && qn < 4) queue[qn++] = a;
}

void eyeSetSleepy(bool s) {
  if (!active || cameo || sad) return;
  if (s) {
    if (sleepy || sleepPending) return;
    sleepPending = true;  // drowsy -> yawn -> asleep
    qn = 0;
    play(findAnim("Somnoliento"));
    enqueue("Bostezo");
    return;
  }
  sleepPending = false;
  if (!sleepy) return;
  sleepy = false;
  play(findAnim("Despertar"));
}

static void layerOpa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
static void layerGone(lv_anim_t* a) {
  if (!active) lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN);
}

static void layerFade(bool in, uint32_t ms) {
  lv_anim_del(scr, layerOpa);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_HIDDEN);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, scr);
  lv_anim_set_exec_cb(&a, layerOpa);
  lv_anim_set_values(&a, lv_obj_get_style_opa(scr, 0), in ? LV_OPA_COVER : LV_OPA_TRANSP);
  lv_anim_set_time(&a, ms);
  if (!in) lv_anim_set_ready_cb(&a, layerGone);
  lv_anim_start(&a);
}

static void popIn() {
  sleepy = sad = sleepPending = false;
  qn = 0;
  layerFade(true, 300);
}

// pop in, play these animations in order, pop out (e.g. "Arranque" + "Saludo" on connect)
uint32_t eyeCameoSeq(const char* const* names, int n) {
  bool wasActive = active;
  if (!wasActive) popIn();
  active = cameo = true;
  sad = sleepy = sleepPending = transition = false;
  qn = 0;
  uint32_t total = 0;
  for (int i = 0; i < n; i++) {
    int a = findAnim(names[i]);
    if (a < 0) continue;
    total += animTotal[a];
    if (i && qn < 4) queue[qn++] = a;
  }
  play(findAnim(names[0]));
  return total;
}

// a reaction played right before the screen changes; nothing hides it early
uint32_t eyeTransition(const char* const* names, int n) {
  uint32_t ms = eyeCameoSeq(names, n);
  transition = true;
  return ms;
}

bool eyeIsTransition() { return active && transition; }

// PC gone: stay sad (now and then a tear) until it's back or the screen goes off
void eyeSad() {
  if (!active) popIn();
  active = sad = true;
  cameo = transition = sleepPending = false;
  qn = 0;
  play(idxSad);                        // sad, then hold it and cry now and then
  restUntil = millis() + 1500;
}

bool eyeIsSad() { return active && sad; }

void eyeCameo() {  // pop in, play one fun animation, pop out
  if (active) return;
  int a;
  do a = random(EYE_ANIM_COUNT);  // something with a bit of show: at least 1.5 s
  while (excluded[a] || a == idxSleep || animTotal[a] < 1500);
  popIn();
  active = cameo = true;
  play(a);
}

bool eyeIsCameo() { return active && cameo; }

void eyeShow() {
  if (active) {
    if (!transition) cameo = false;  // a cameo that turns into a real idle session stays up
    return;
  }
  cameo = false;
  active = true;
  sleepy = false;
  play(idxBoot);
  layerFade(true, 500);
}

void eyeHide() {
  if (!active) return;
  active = cameo = sad = transition = sleepPending = false;
  qn = 0;
  cur = -1;
  layerFade(false, 300);
}

bool eyeActive() { return active; }
bool eyeDismissed() { return dismissed; }
void eyeClearDismiss() { dismissed = false; }
void eyePlay(int idx) { play(idx); }

void eyeReact(const char* name) {
  if (active) play(findAnim(name));
}

void eyeTick() {
  if (!active) return;
  uint32_t now = millis();
  if (cur >= 0) {
    uint32_t tau = now - t0;
    if (tau >= animTotal[cur]) {
      cur = -1;
      if (sad) {
        lv_obj_invalidate(view);
        return;
      }
      if (qn) {  // next animation of a sequence
        play(queue[0]);
        for (int i = 1; i < qn; i++) queue[i - 1] = queue[i];
        qn--;
        lv_obj_invalidate(view);
        return;
      }
      if (cameo) {
        eyeHide();
        return;
      }
      if (sleepPending) {  // the drowsy sequence is over: fall asleep
        sleepPending = false;
        sleepy = true;
        lv_obj_invalidate(view);
        return;
      }
      st = BASE_S;
      restUntil = now + random(1200, 4200);
    } else {
      st = sample(cur, tau);
    }
  } else if (sad) {  // hold the sad face, cry every so often
    static const int SAD_HOLD = 1000;
    st = sample(idxSad, SAD_HOLD);
    if ((int32_t)(now - restUntil) >= 0) {
      play(idxCry);
      restUntil = now + random(6000, 12000);
    }
  } else if (!sleepy && (int32_t)(now - restUntil) >= 0) {
    pickNext();
  }
  lv_obj_invalidate(view);
}
