// Idle companion: a pair of robot eyes that blink, look around, smile when
// touched and doze off when you've been away for a while.
// Tap = happy. Long-press = dismiss until you use the PC again.
#include "app.h"
#include "theme.h"

#define EYE_W 54
#define EYE_H 76
#define EYE_GAP 42
#define EYE_COLOR lv_color_hex(0x5AC8FA)

static lv_obj_t *scr, *prevScr, *face, *eyes[2], *cheeks[2], *zzz;
static bool active = false, sleepy = false, dismissed = false;
static uint32_t nextBlink = 0, nextLook = 0, happyUntil = 0;
static int lookX = 0, lookY = 0;

static void setH(void* o, int32_t v) { lv_obj_set_height((lv_obj_t*)o, v); }
static void setTx(void* o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); }
static void setTy(void* o, int32_t v) { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); }
static void setOpa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, v, 0); }

static void anim(void* obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                 uint32_t back = 0, uint32_t delay = 0, lv_anim_path_cb_t path = lv_anim_path_ease_in_out,
                 int32_t repeat = 0) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_path_cb(&a, path);
  if (back) lv_anim_set_playback_time(&a, back);
  if (repeat) lv_anim_set_repeat_count(&a, repeat);
  lv_anim_start(&a);
}

static int openHeight() { return sleepy ? 8 : EYE_H; }

static void blink() {
  for (lv_obj_t* e : eyes) anim(e, setH, openHeight(), 6, 70, 110);
}

static void look(int x, int y) {
  anim(face, setTx, lookX, x, 260);
  anim(face, setTy, lookY, y, 260);
  lookX = x;
  lookY = y;
}

static void showCheeks(bool on) {
  for (lv_obj_t* c : cheeks) {
    if (on) lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
  }
}

static void happy() {
  happyUntil = millis() + 1800;
  if (sleepy) eyeSetSleepy(false);
  for (lv_obj_t* e : eyes) {
    lv_anim_del(e, setH);
    lv_obj_set_height(e, EYE_H);
  }
  showCheeks(true);
  look(0, -6);
  anim(face, setTy, -6, -14, 160, 160, 0, lv_anim_path_ease_out, 2);
}

static void eventCb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_SHORT_CLICKED) happy();
  else if (c == LV_EVENT_LONG_PRESSED) {
    dismissed = true;
    eyeHide();
  }
}

void eyeInit() {
  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, eventCb, LV_EVENT_SHORT_CLICKED, NULL);
  lv_obj_add_event_cb(scr, eventCb, LV_EVENT_LONG_PRESSED, NULL);

  face = uiBox(scr);
  lv_obj_set_size(face, 240, 240);
  lv_obj_center(face);
  for (int i = 0; i < 2; i++) {
    int x = i ? EYE_GAP : -EYE_GAP;
    eyes[i] = uiBox(face);
    lv_obj_set_size(eyes[i], EYE_W, EYE_H);
    lv_obj_set_style_radius(eyes[i], 22, 0);
    lv_obj_set_style_bg_opa(eyes[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(eyes[i], EYE_COLOR, 0);
    lv_obj_align(eyes[i], LV_ALIGN_CENTER, x, 0);
    // black disc that bites the bottom of the eye -> "^ ^" happy eyes
    cheeks[i] = uiBox(face);
    lv_obj_set_size(cheeks[i], 84, 84);
    lv_obj_set_style_radius(cheeks[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cheeks[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cheeks[i], C_BG, 0);
    lv_obj_align(cheeks[i], LV_ALIGN_CENTER, x, 54);
    lv_obj_add_flag(cheeks[i], LV_OBJ_FLAG_HIDDEN);
  }
  zzz = uiLabel(scr, &font_value, C_TXT2, "z");
  lv_obj_align(zzz, LV_ALIGN_CENTER, 62, -46);
  lv_obj_add_flag(zzz, LV_OBJ_FLAG_HIDDEN);
}

void eyeSetSleepy(bool s) {
  if (s == sleepy || !active) return;
  sleepy = s;
  for (lv_obj_t* e : eyes) {
    lv_anim_del(e, setH);
    anim(e, setH, lv_obj_get_height(e), openHeight(), s ? 900 : 250);
  }
  if (s) {
    look(0, 18);
    lv_obj_clear_flag(zzz, LV_OBJ_FLAG_HIDDEN);
    anim(zzz, setTy, 10, -18, 2200, 0, 0, lv_anim_path_linear, LV_ANIM_REPEAT_INFINITE);
    anim(zzz, setOpa, 255, 0, 2200, 0, 0, lv_anim_path_ease_in, LV_ANIM_REPEAT_INFINITE);
  } else {
    lv_anim_del(zzz, NULL);
    lv_obj_add_flag(zzz, LV_OBJ_FLAG_HIDDEN);
    look(0, 0);
  }
}

void eyeShow() {
  if (active) return;
  active = true;
  sleepy = false;
  showCheeks(false);
  for (lv_obj_t* e : eyes) lv_obj_set_height(e, EYE_H);
  nextBlink = millis() + 1200;
  nextLook = millis() + 2500;
  prevScr = lv_scr_act();
  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
}

void eyeHide() {
  if (!active) return;
  active = false;
  lv_anim_del(zzz, NULL);
  lv_obj_add_flag(zzz, LV_OBJ_FLAG_HIDDEN);
  lv_scr_load_anim(prevScr, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

bool eyeActive() { return active; }
bool eyeDismissed() { return dismissed; }
void eyeClearDismiss() { dismissed = false; }

void eyeTick() {
  if (!active) return;
  uint32_t now = millis();
  if (happyUntil && now > happyUntil) {
    happyUntil = 0;
    showCheeks(false);
  }
  if (sleepy || happyUntil) return;
  if (now > nextBlink) {
    blink();
    // sometimes a double blink
    nextBlink = now + (random(5) == 0 ? 260 : random(2200, 6000));
  }
  if (now > nextLook) {
    if (random(3) == 0) look(0, 0);
    else look(random(-30, 31), random(-18, 19));
    nextLook = now + random(1200, 4200);
  }
}
