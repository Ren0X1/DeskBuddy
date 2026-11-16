#include "hud.h"
#include <math.h>

void hudBegin(Hud& h, lv_event_t* e) {
  h.ctx = lv_event_get_draw_ctx(e);
  lv_obj_t* o = lv_event_get_target(e);
  h.ox = o->coords.x1;
  h.oy = o->coords.y1;
}

void hudLine(Hud& h, float x1, float y1, float x2, float y2, lv_color_t c, int w, lv_opa_t opa) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = c;
  d.width = w;
  d.opa = opa;
  lv_point_t a = {(lv_coord_t)(h.ox + lroundf(x1)), (lv_coord_t)(h.oy + lroundf(y1))};
  lv_point_t b = {(lv_coord_t)(h.ox + lroundf(x2)), (lv_coord_t)(h.oy + lroundf(y2))};
  lv_draw_line(h.ctx, &d, &a, &b);
}

int hudTextWidth(const char* s, const lv_font_t* f) {
  lv_point_t sz;
  lv_txt_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}

void hudText(Hud& h, const char* s, int x, int y, const lv_font_t* f, lv_color_t c, int align,
             lv_opa_t opa) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = f;
  d.color = c;
  d.opa = opa;
  lv_point_t sz;
  lv_txt_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_area_t a;
  a.x1 = h.ox + x - (align == HUD_CENTER ? sz.x / 2 : align == HUD_RIGHT ? sz.x : 0);
  a.y1 = h.oy + y - sz.y / 2;
  a.x2 = a.x1 + sz.x;
  a.y2 = a.y1 + sz.y;
  lv_draw_label(h.ctx, &d, &a, s, NULL);
}

void hudRect(Hud& h, int x, int y, int w, int hgt, lv_color_t c, int border, int radius, lv_opa_t opa) {
  if (w <= 0 || hgt <= 0) return;
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = radius;
  if (border) {
    d.bg_opa = LV_OPA_TRANSP;
    d.border_width = border;
    d.border_color = c;
    d.border_opa = opa;
  } else {
    d.bg_color = c;
    d.bg_opa = opa;
  }
  lv_area_t a = {(lv_coord_t)(h.ox + x), (lv_coord_t)(h.oy + y), (lv_coord_t)(h.ox + x + w - 1),
                 (lv_coord_t)(h.oy + y + hgt - 1)};
  lv_draw_rect(h.ctx, &d, &a);
}

void hudArc(Hud& h, int cx, int cy, int r, int w, int a0, int a1, lv_color_t c, lv_opa_t opa,
            bool rounded) {
  if (r <= 0) return;
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.color = c;
  d.width = w;
  d.opa = opa;
  d.rounded = rounded;
  lv_point_t ctr = {(lv_coord_t)(h.ox + cx), (lv_coord_t)(h.oy + cy)};
  if (a1 - a0 >= 360) {  // full circle: normalising would turn 0..360 into 0..0 (nothing)
    a0 = 0;
    a1 = 360;
  } else {
    a0 = ((a0 % 360) + 360) % 360;
    a1 = ((a1 % 360) + 360) % 360;
  }
  lv_draw_arc(h.ctx, &d, &ctr, r, a0, a1);
}

void hudCircle(Hud& h, int cx, int cy, int r, lv_color_t c, lv_opa_t opa) {
  hudRect(h, cx - r, cy - r, 2 * r + 1, 2 * r + 1, c, 0, LV_RADIUS_CIRCLE, opa);
}

void hudRing(Hud& h, int cx, int cy, int r, int w, lv_color_t c, lv_opa_t opa) {
  hudArc(h, cx, cy, r, w, 0, 360, c, opa);
}

void hudPoly(Hud& h, const lv_point_t* pts, int n, lv_color_t c, lv_opa_t opa) {
  if (n < 3 || n > 16) return;
  lv_point_t p[16];
  for (int i = 0; i < n; i++) p[i] = {(lv_coord_t)(pts[i].x + h.ox), (lv_coord_t)(pts[i].y + h.oy)};
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = c;
  d.bg_opa = opa;
  lv_draw_polygon(h.ctx, &d, p, n);
}

void hudTri(Hud& h, float x, float y, float s, char dir, lv_color_t c) {
  lv_point_t p[3];
  switch (dir) {
    case 'r': p[0] = {(lv_coord_t)(x - s), (lv_coord_t)(y - s)}; p[1] = {(lv_coord_t)(x + s), (lv_coord_t)y};
              p[2] = {(lv_coord_t)(x - s), (lv_coord_t)(y + s)}; break;
    case 'l': p[0] = {(lv_coord_t)(x + s), (lv_coord_t)(y - s)}; p[1] = {(lv_coord_t)(x - s), (lv_coord_t)y};
              p[2] = {(lv_coord_t)(x + s), (lv_coord_t)(y + s)}; break;
    case 'd': p[0] = {(lv_coord_t)(x - s), (lv_coord_t)(y - s)}; p[1] = {(lv_coord_t)(x + s), (lv_coord_t)(y - s)};
              p[2] = {(lv_coord_t)x, (lv_coord_t)(y + s)}; break;
    default:  p[0] = {(lv_coord_t)(x - s), (lv_coord_t)(y + s)}; p[1] = {(lv_coord_t)(x + s), (lv_coord_t)(y + s)};
              p[2] = {(lv_coord_t)x, (lv_coord_t)(y - s)}; break;
  }
  hudPoly(h, p, 3, c);
}

void hudScanlines(Hud& h, lv_opa_t opa) {
  for (int y = 1; y < 240; y += 3) hudLine(h, 0, y, 239, y, lv_color_black(), 1, opa);
}

static lv_area_t clipStack[6];
static const lv_area_t* clipSaved[6];
static int clipDepth = 0;

bool hudClipPush(Hud& h, int x, int y, int w, int hgt) {
  if (clipDepth >= 6) return false;
  lv_area_t want = {(lv_coord_t)(h.ox + x), (lv_coord_t)(h.oy + y), (lv_coord_t)(h.ox + x + w - 1),
                    (lv_coord_t)(h.oy + y + hgt - 1)};
  clipSaved[clipDepth] = h.ctx->clip_area;
  lv_area_t& r = clipStack[clipDepth];
  bool ok = _lv_area_intersect(&r, h.ctx->clip_area, &want);
  if (!ok) r = {1, 1, 0, 0};  // empty
  h.ctx->clip_area = &r;
  clipDepth++;
  return ok;
}

void hudClipPop(Hud& h) {
  if (clipDepth <= 0) return;
  clipDepth--;
  h.ctx->clip_area = clipSaved[clipDepth];
}
