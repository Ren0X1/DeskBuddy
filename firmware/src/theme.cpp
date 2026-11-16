#include "theme.h"
#include "app.h"

lv_obj_t* uiLabel(lv_obj_t* p, const lv_font_t* f, lv_color_t c, const char* txt) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c, 0);
  lv_label_set_text(l, txt);
  return l;
}

lv_obj_t* uiCaption(lv_obj_t* p, const char* txt, lv_color_t c) {
  lv_obj_t* l = uiLabel(p, &font_cap, c, txt);
  lv_obj_set_style_text_letter_space(l, 2, 0);
  return l;
}

lv_obj_t* uiBox(lv_obj_t* p) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t* uiRow(lv_obj_t* p, int gap) {
  lv_obj_t* r = uiBox(p);
  lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(r, gap, 0);
  return r;
}

lv_obj_t* uiArc(lv_obj_t* p, int size, int width, lv_color_t col, int rot, int span) {
  lv_obj_t* a = lv_arc_create(p);
  lv_obj_set_size(a, size, size);
  lv_obj_center(a);
  lv_arc_set_rotation(a, rot);
  lv_arc_set_bg_angles(a, 0, span);
  lv_arc_set_range(a, 0, 1000);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(a, 0, 0);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C_SURFACE, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, col, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  return a;
}

lv_obj_t* uiBar(lv_obj_t* p, int w, int h, lv_color_t col) {
  lv_obj_t* b = lv_bar_create(p);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, w, h);
  lv_bar_set_range(b, 0, 1000);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(b, C_SURFACE2, LV_PART_MAIN);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b, col, LV_PART_INDICATOR);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  return b;
}

lv_obj_t* uiTickMeter(lv_obj_t* p, lv_meter_scale_t** scale, int ticks, int nth, int minW, int len,
                      int majorLen, int maxV, int angle, int rot) {
  lv_obj_t* m = lv_meter_create(p);
  lv_obj_remove_style_all(m);
  lv_obj_set_size(m, 236, 236);
  lv_obj_center(m);
  lv_obj_clear_flag(m, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_opa(m, LV_OPA_TRANSP, LV_PART_TICKS);
  *scale = lv_meter_add_scale(m);
  lv_meter_set_scale_ticks(m, *scale, ticks, minW, len, C_TICK);
  lv_meter_set_scale_major_ticks(m, *scale, nth, minW + 1, majorLen, C_TICK_MAJOR, 10);
  lv_meter_set_scale_range(m, *scale, 0, maxV, angle, rot);
  return m;
}

static void cmdCb(lv_event_t* e) { netSendCmd((const char*)lv_event_get_user_data(e)); }

lv_obj_t* uiIconBtn(lv_obj_t* p, const char* icon, const lv_font_t* f, int size, lv_color_t bg,
                    lv_color_t fg, const char* cmd) {
  lv_obj_t* b = lv_btn_create(p);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, size, size);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_bg_color(b, lv_color_mix(C_TXT, bg, 50), LV_STATE_PRESSED);
  lv_obj_set_style_transform_width(b, -2, LV_STATE_PRESSED);
  lv_obj_set_style_transform_height(b, -2, LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(b, 6);
  lv_obj_t* l = uiLabel(b, f, fg, icon);
  lv_obj_center(l);
  if (cmd) lv_obj_add_event_cb(b, cmdCb, LV_EVENT_CLICKED, (void*)cmd);
  return b;
}

void uiSetText(lv_obj_t* l, const char* txt) {
  if (strcmp(lv_label_get_text(l), txt)) lv_label_set_text(l, txt);
}

lv_color_t uiLoadColor(float pct, lv_color_t base) {
  return pct >= 90 ? C_HOT : pct >= 75 ? C_WARN : base;
}
