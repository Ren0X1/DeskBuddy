// Now-playing screen: album art, stereo VU, progress ring and media controls.
#include "app.h"
#include "theme.h"

static lv_obj_t *arcProg, *artBox, *artImg, *artPh, *vuL, *vuR;
static lv_obj_t *lblTitle, *lblArtist, *lblPlay, *btnPlay, *volBar, *lblVolIcon;
static lv_img_dsc_t artDsc;
static uint16_t shownArt = 0;
static uint32_t shownAccent = 1;
static float vl = 0, vr = 0;

static void muteCb(lv_event_t*) { netSendCmd("mute"); }

static lv_obj_t* vuBar(lv_obj_t* t, int x) {
  lv_obj_t* b = uiBar(t, 4, 64, C_ACCENT);
  lv_obj_set_style_bg_color(b, C_SURFACE, LV_PART_MAIN);
  lv_obj_align(b, LV_ALIGN_CENTER, x, -48);
  return b;
}

void musicBuild(lv_obj_t* t) {
  arcProg = uiArc(t, 232, 4, C_ACCENT, 135, 270);

  // album art (rounded) with a placeholder note
  artBox = uiBox(t);
  lv_obj_set_size(artBox, ART_SIZE, ART_SIZE);
  lv_obj_set_style_radius(artBox, 16, 0);
  lv_obj_set_style_clip_corner(artBox, true, 0);
  lv_obj_set_style_bg_opa(artBox, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(artBox, C_SURFACE, 0);
  lv_obj_align(artBox, LV_ALIGN_CENTER, 0, -48);
  artPh = uiLabel(artBox, &icons_32, C_TXT3, ICON_MUSIC);
  lv_obj_center(artPh);
  artImg = lv_img_create(artBox);
  lv_obj_center(artImg);
  lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);
  artDsc.header.always_zero = 0;
  artDsc.header.w = ART_SIZE;
  artDsc.header.h = ART_SIZE;
  artDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  artDsc.data_size = sizeof(artPixels);
  artDsc.data = (const uint8_t*)artPixels;

  vuL = vuBar(t, -ART_SIZE / 2 - 14);
  vuR = vuBar(t, ART_SIZE / 2 + 14);

  lblTitle = uiLabel(t, &font_body, C_TXT, "");
  lv_obj_set_width(lblTitle, 172);
  lv_obj_set_style_text_align(lblTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_anim_speed(lblTitle, 22, 0);
  lv_obj_align(lblTitle, LV_ALIGN_CENTER, 0, 8);

  lblArtist = uiLabel(t, &font_small, C_TXT2, "");
  lv_obj_set_width(lblArtist, 160);
  lv_obj_set_style_text_align(lblArtist, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(lblArtist, LV_LABEL_LONG_DOT);
  lv_obj_align(lblArtist, LV_ALIGN_CENTER, 0, 27);

  lv_obj_t* prev = uiIconBtn(t, ICON_PREV, &icons_32, 40, C_BG, C_TXT, "prev");
  lv_obj_align(prev, LV_ALIGN_CENTER, -58, 58);
  btnPlay = uiIconBtn(t, ICON_PLAY, &icons_32, 46, C_TXT, C_BG, "play");
  lv_obj_align(btnPlay, LV_ALIGN_CENTER, 0, 58);
  lblPlay = lv_obj_get_child(btnPlay, 0);
  lv_obj_t* next = uiIconBtn(t, ICON_NEXT, &icons_32, 40, C_BG, C_TXT, "next");
  lv_obj_align(next, LV_ALIGN_CENTER, 58, 58);

  lv_obj_t* dn = uiIconBtn(t, ICON_MINUS, &icons_16, 24, C_SURFACE, C_TXT2, "voldn");
  lv_obj_align(dn, LV_ALIGN_CENTER, -52, 92);
  lv_obj_t* up = uiIconBtn(t, ICON_PLUS, &icons_16, 24, C_SURFACE, C_TXT2, "volup");
  lv_obj_align(up, LV_ALIGN_CENTER, 52, 92);
  lblVolIcon = uiLabel(t, &icons_16, C_TXT2, ICON_VOL);
  lv_obj_align(lblVolIcon, LV_ALIGN_CENTER, -24, 92);
  volBar = uiBar(t, 44, 4, C_TXT2);
  lv_obj_align(volBar, LV_ALIGN_CENTER, 8, 92);
  for (lv_obj_t* o : {lblVolIcon, volBar}) {
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(o, 10);
    lv_obj_add_event_cb(o, muteCb, LV_EVENT_CLICKED, NULL);
  }
}

static void applyAccent(lv_color_t a) {
  lv_obj_set_style_arc_color(arcProg, a, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(vuL, a, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(vuR, a, LV_PART_INDICATOR);
}

void musicTick() {
  bool on = pcOnline() && media.has;

  uint32_t acc = on && media.accent ? media.accent : 0x5AC8FA;
  if (acc != shownAccent) {
    shownAccent = acc;
    applyAccent(lv_color_hex(acc));
  }

  // album art
  uint16_t want = on ? media.artId : 0;
  if (want && artReadyId == want && shownArt != want) {
    shownArt = want;
    lv_img_cache_invalidate_src(&artDsc);
    lv_img_set_src(artImg, &artDsc);
    lv_obj_clear_flag(artImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(artPh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(artImg);
  } else if (!want || (artReadyId != want && shownArt != want)) {
    if (shownArt) {
      shownArt = 0;
      lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(artPh, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (on) {
    uiSetText(lblTitle, media.title[0] ? media.title : "Sin título");
    uiSetText(lblArtist, media.artist[0] ? media.artist : media.app);
    float pos = media.pos + (media.playing ? (millis() - media.rxAt) / 1000.0f : 0);
    lv_arc_set_value(arcProg, media.dur > 0 ? (int)(min(pos, media.dur) / media.dur * 1000) : 0);
  } else {
    uiSetText(lblTitle, "Nada sonando");
    uiSetText(lblArtist, pcOnline() ? "Pon algo de música" : "PC desconectado");
    lv_arc_set_value(arcProg, 0);
  }
  uiSetText(lblPlay, on && media.playing ? ICON_PAUSE : ICON_PLAY);

  lv_bar_set_value(volBar, pcOnline() ? media.vol * 10 : 0, LV_ANIM_ON);
  uiSetText(lblVolIcon, media.muted ? ICON_MUTE : ICON_VOL);
  lv_color_t vc = media.muted ? C_HOT : C_TXT2;
  lv_obj_set_style_text_color(lblVolIcon, vc, 0);
  lv_obj_set_style_bg_color(volBar, vc, LV_PART_INDICATOR);
}

void musicFast() {
  // fast attack, slow release
  vl = vuTargetL > vl ? vl + (vuTargetL - vl) * 0.6f : vl * 0.86f;
  vr = vuTargetR > vr ? vr + (vuTargetR - vr) * 0.6f : vr * 0.86f;
  vuTargetL *= 0.88f;
  vuTargetR *= 0.88f;
  lv_bar_set_value(vuL, (int)(vl * 1000), LV_ANIM_OFF);
  lv_bar_set_value(vuR, (int)(vr * 1000), LV_ANIM_OFF);
}
