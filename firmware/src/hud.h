#pragma once
// Immediate-mode drawing for the HUD screens: call from an LV_EVENT_DRAW_MAIN handler.
// Coordinates are local to the object (0..239), like the web preview's canvas.
#include <lvgl.h>

struct Hud {
  lv_draw_ctx_t* ctx;
  lv_coord_t ox, oy;
};

enum { HUD_LEFT = -1, HUD_CENTER = 0, HUD_RIGHT = 1 };

void hudBegin(Hud& h, lv_event_t* e);
void hudLine(Hud& h, float x1, float y1, float x2, float y2, lv_color_t c, int w = 2,
             lv_opa_t opa = LV_OPA_COVER);
void hudText(Hud& h, const char* s, int x, int y, const lv_font_t* f, lv_color_t c,
             int align = HUD_CENTER, lv_opa_t opa = LV_OPA_COVER);
int hudTextWidth(const char* s, const lv_font_t* f);
void hudRect(Hud& h, int x, int y, int w, int hgt, lv_color_t c, int border = 0, int radius = 0,
             lv_opa_t opa = LV_OPA_COVER);
void hudArc(Hud& h, int cx, int cy, int r, int w, int a0, int a1, lv_color_t c,
            lv_opa_t opa = LV_OPA_COVER, bool rounded = false);  // LVGL angles: 0 = east, cw
void hudCircle(Hud& h, int cx, int cy, int r, lv_color_t c, lv_opa_t opa = LV_OPA_COVER);
void hudRing(Hud& h, int cx, int cy, int r, int w, lv_color_t c, lv_opa_t opa = LV_OPA_COVER);
void hudPoly(Hud& h, const lv_point_t* pts, int n, lv_color_t c, lv_opa_t opa = LV_OPA_COVER);
void hudTri(Hud& h, float x, float y, float s, char dir, lv_color_t c);  // dir: l r u d
void hudScanlines(Hud& h, lv_opa_t opa);
// restrict drawing to a box (stackable); returns false if nothing is visible
bool hudClipPush(Hud& h, int x, int y, int w, int hgt);
void hudClipPop(Hud& h);
