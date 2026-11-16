// Now-playing screen, combat-HUD style: the cover sits in a target-designator box,
// the title runs as a marquee, progress is a range staple and the controls are soft keys.
#include "app.h"
#include "hud.h"
#include "theme.h"

#define ART_X 88
#define ART_Y 46
static lv_obj_t *view, *artImg;
static lv_img_dsc_t artDsc;
static uint16_t shownArt = 0;
static float vl = 0, vr = 0;
static uint32_t marquee0 = 0;
static char lastTitle[96] = "";

static void keyCb(lv_event_t* e) { netSendCmd((const char*)lv_event_get_user_data(e)); }

// invisible touch targets on top of the drawn soft keys
static void key(lv_obj_t* t, int x, int y, int w, int h, const char* cmd) {
  lv_obj_t* b = lv_btn_create(t);
  lv_obj_remove_style_all(b);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_ext_click_area(b, 6);
  lv_obj_add_event_cb(b, keyCb, LV_EVENT_CLICKED, (void*)cmd);
}

static void drawKey(Hud& h, int cx, char kind, bool lit) {
  lv_color_t c = lit ? G_BRIGHT : G_MID;
  hudRect(h, cx - 15, 190, 30, 20, c, 2);
  if (kind == 'p') {
    if (media.playing) {
      hudRect(h, cx - 5, 194, 3, 12, c);
      hudRect(h, cx + 2, 194, 3, 12, c);
    } else {
      hudTri(h, cx + 1, 200, 5, 'r', c);
    }
  } else {
    int s = kind == 'l' ? -1 : 1;
    hudTri(h, cx + 3 * s, 200, 4, kind, c);
    hudLine(h, cx + 8 * s, 195, cx + 8 * s, 205, c, 2);
  }
}

static void fmt(char* out, size_t n, float s) {
  int t = (int)s;
  snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

static void draw(lv_event_t* e) {
  Hud h;
  hudBegin(h, e);
  bool on = pcOnline() && media.has;

  // target designator box (the cover image is a child object inside it)
  if (!(on && shownArt)) {
    hudLine(h, ART_X + 32, ART_Y + 14, ART_X + 50, ART_Y + 32, G_BRIGHT, 2);
    hudLine(h, ART_X + 50, ART_Y + 32, ART_X + 32, ART_Y + 50, G_BRIGHT, 2);
    hudLine(h, ART_X + 32, ART_Y + 50, ART_X + 14, ART_Y + 32, G_BRIGHT, 2);
    hudLine(h, ART_X + 14, ART_Y + 32, ART_X + 32, ART_Y + 14, G_BRIGHT, 2);
    for (int i = -64; i < 128; i += 8)
      if (hudClipPush(h, ART_X, ART_Y, 64, 64)) {
        hudLine(h, ART_X + i, ART_Y, ART_X + i + 64, ART_Y + 64, G_FAINT, 1);
        hudClipPop(h);
      } else hudClipPop(h);
  }
  hudRect(h, ART_X - 2, ART_Y - 2, 68, 68, G_BRIGHT, 2);
  hudText(h, "TGT", ART_X - 8, ART_Y + 6, &font_tiny, G_MID, HUD_RIGHT);

  // VU tapes either side
  float vu[2] = {vl, vr};
  for (int i = 0; i < 2; i++) {
    int x = i ? 180 : 60;
    hudRect(h, x - 4, 48, 8, 60, G_DIM, 1);
    int hh = (int)(56 * min(1.0f, vu[i]));
    hudRect(h, x - 2, 106 - hh, 4, hh, vu[i] > .92f ? C_WARN : G_BRIGHT);
  }

  // marquee title
  const char* title = on ? (media.title[0] ? media.title : "SIN TÍTULO") : "NADA SONANDO";
  int tw = hudTextWidth(title, &font_body);
  if (hudClipPush(h, 46, 120, 148, 22)) {
    if (tw <= 148) {
      hudText(h, title, 120, 131, &font_body, G_BRIGHT);
    } else {
      char buf[120];
      snprintf(buf, sizeof buf, "%s   •   ", title);
      int w = hudTextWidth(buf, &font_body);
      int off = ((millis() - marquee0) / 30) % w;
      hudText(h, buf, 46 - off, 131, &font_body, G_BRIGHT, HUD_LEFT);
      hudText(h, buf, 46 - off + w, 131, &font_body, G_BRIGHT, HUD_LEFT);
    }
  }
  hudClipPop(h);
  const char* sub = on ? (media.artist[0] ? media.artist : media.app) : (pcOnline() ? "PON MÚSICA" : "PC DESCONECTADO");
  hudText(h, sub, 120, 150, &font_cap, G_MID);

  // range staple = song progress, volume in the middle
  hudLine(h, 56, 168, 184, 168, G_DIM, 2);
  hudLine(h, 56, 163, 56, 173, G_BRIGHT, 2);
  hudLine(h, 184, 163, 184, 173, G_BRIGHT, 2);
  char a[12], b[12], v[16];
  if (on && media.dur > 0) {
    float pos = media.pos + (media.playing ? (millis() - media.rxAt) / 1000.0f : 0);
    pos = min(pos, media.dur);
    hudTri(h, 56 + 128 * pos / media.dur, 162, 4, 'd', G_BRIGHT);
    fmt(a, sizeof a, pos);
    fmt(b, sizeof b, media.dur);
    hudText(h, a, 56, 180, &font_tiny, G_MID, HUD_LEFT);
    hudText(h, b, 184, 180, &font_tiny, G_MID, HUD_RIGHT);
  }
  if (pcOnline()) {
    snprintf(v, sizeof v, media.muted ? "MUTE" : "VOL %d", media.vol);
    hudText(h, v, 120, 180, &font_tiny, media.muted ? C_WARN : G_DIM);
  }

  // soft keys: VOL- PREV PLAY NEXT VOL+
  hudRect(h, 38, 190, 22, 20, G_DIM, 1);
  hudText(h, "-", 49, 200, &font_body, G_MID);
  drawKey(h, 82, 'l', false);
  drawKey(h, 120, 'p', true);
  drawKey(h, 158, 'r', false);
  hudRect(h, 180, 190, 22, 20, G_DIM, 1);
  hudText(h, "+", 191, 200, &font_body, G_MID);
}

void musicBuild(lv_obj_t* t) {
  view = uiBox(t);
  lv_obj_set_size(view, 240, 240);
  lv_obj_add_event_cb(view, draw, LV_EVENT_DRAW_MAIN, NULL);

  artImg = lv_img_create(t);
  // lv_img_set_src() re-centres the pivot, so offset the 80 px image: scaled from its centre
  // to 64 px it lands exactly in the target box
  lv_obj_set_pos(artImg, ART_X - (ART_SIZE - 64) / 2, ART_Y - (ART_SIZE - 64) / 2);
  lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);
  artDsc.header.always_zero = 0;
  artDsc.header.w = ART_SIZE;
  artDsc.header.h = ART_SIZE;
  artDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  artDsc.data_size = sizeof(artPixels);
  artDsc.data = (const uint8_t*)artPixels;
  lv_img_set_zoom(artImg, 256 * 64 / ART_SIZE);  // 80 px cover -> 64 px box

  key(t, 38, 190, 22, 20, "voldn");
  key(t, 67, 190, 30, 20, "prev");
  key(t, 105, 190, 30, 20, "play");
  key(t, 143, 190, 30, 20, "next");
  key(t, 180, 190, 22, 20, "volup");
  // tap the cover/volume to mute
  key(t, 104, 174, 32, 12, "mute");
}

void musicTick() {
  bool on = pcOnline() && media.has;
  uint16_t want = on ? media.artId : 0;
  if (want && artReadyId == want && shownArt != want) {
    shownArt = want;
    lv_img_cache_invalidate_src(&artDsc);
    lv_img_set_src(artImg, &artDsc);
    lv_obj_clear_flag(artImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(artImg);
  } else if (!want && shownArt) {
    shownArt = 0;
    lv_obj_add_flag(artImg, LV_OBJ_FLAG_HIDDEN);
  }
  if (strcmp(lastTitle, media.title)) {
    strlcpy(lastTitle, media.title, sizeof lastTitle);
    marquee0 = millis();
  }
}

float musicLevel() { return max(vl, vr); }

void musicFast() {
  vl = vuTargetL > vl ? vl + (vuTargetL - vl) * 0.6f : vl * 0.86f;
  vr = vuTargetR > vr ? vr + (vuTargetR - vr) * 0.6f : vr * 0.86f;
  vuTargetL *= 0.88f;
  vuTargetR *= 0.88f;
  if (uiTile() == TILE_MUSIC) lv_obj_invalidate(view);
}
