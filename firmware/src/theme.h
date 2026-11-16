#pragma once
#include <lvgl.h>
#include "fonts/fonts.h"

// Palette: true black, a neutral grey ramp and one accent.
#define C_BG lv_color_hex(0x000000)
#define C_SURFACE lv_color_hex(0x16181D)
#define C_SURFACE2 lv_color_hex(0x24272E)
#define C_TICK lv_color_hex(0x202329)
#define C_TICK_MAJOR lv_color_hex(0x3A3F48)
#define C_TXT lv_color_hex(0xF2F4F7)
#define C_TXT2 lv_color_hex(0x8B93A1)
#define C_TXT3 lv_color_hex(0x565D69)
#define C_ACCENT lv_color_hex(0x5AC8FA)
#define C_OK lv_color_hex(0x32D74B)
#define C_WARN lv_color_hex(0xFFB020)
#define C_HOT lv_color_hex(0xFF453A)
#define C_HOT_DIM lv_color_hex(0x3D1513)

lv_obj_t* uiLabel(lv_obj_t* p, const lv_font_t* f, lv_color_t c, const char* txt);
lv_obj_t* uiCaption(lv_obj_t* p, const char* txt, lv_color_t c = C_TXT2);
lv_obj_t* uiBox(lv_obj_t* p);
lv_obj_t* uiRow(lv_obj_t* p, int gap = 4);
lv_obj_t* uiArc(lv_obj_t* p, int size, int width, lv_color_t col, int rot, int span);
lv_obj_t* uiBar(lv_obj_t* p, int w, int h, lv_color_t col);
lv_obj_t* uiTickMeter(lv_obj_t* p, lv_meter_scale_t** scale, int ticks, int nth, int minW, int len,
                      int majorLen, int maxV, int angle, int rot);
lv_obj_t* uiIconBtn(lv_obj_t* p, const char* icon, const lv_font_t* f, int size, lv_color_t bg,
                    lv_color_t fg, const char* cmd);
void uiSetText(lv_obj_t* l, const char* txt);
lv_color_t uiLoadColor(float pct, lv_color_t base);
