#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"

enum Tile { TILE_CLOCK, TILE_MON, TILE_MUSIC, TILE_INFO, TILE_COUNT };

struct PcStats {
  float cpu = 0, ram = 0, gpu = 0, gpuTemp = 0, vram = 0;
  uint32_t idle = 0;  // seconds since last keyboard/mouse input on the PC
  char host[24] = "";
  uint32_t lastRx = 0;
};

struct MediaInfo {
  bool has = false, playing = false, muted = false;
  char title[96] = "", artist[64] = "", app[24] = "";
  float pos = 0, dur = 0;
  int vol = 0;
  uint32_t rxAt = 0;
  uint32_t accent = 0;  // album-art color, 0 = default
  uint16_t artId = 0;   // 0 = no art
};

struct Weather {
  bool valid = false, isDay = true;
  float temp = 0, tmax = 0, tmin = 0;
  int code = 0;
  char city[32] = "";
};

extern PcStats pc;
extern MediaInfo media;
extern Weather weather;
extern float vuTargetL, vuTargetR;
extern uint16_t artPixels[ART_SIZE * ART_SIZE];
extern volatile uint16_t artReadyId;

bool pcOnline();         // got stats in the last 5 s
bool pcAsleep();         // nothing from the PC for a minute
bool isNight();
uint32_t lastTouchMs();

// hw.cpp - display, touch, backlight, serial debug
void hwInit();
void hwLoop();
uint8_t hwUserBrightness();
void hwSetUserBrightness(uint8_t b, bool save);
void hwSetTarget(uint8_t b);

// net.cpp - Wi-Fi, setup portal, UDP, OTA, weather
void netInit();
void netLoop();
void netSendCmd(const char* cmd);
void netResetWifi();
const char* netSsid();

// ui_main.cpp
void uiInit();
void uiGoTile(int tile, bool anim = true);
int uiTile();
void uiOtaProgress(int pct);
void uiPortal(bool on);
void uiOnMediaStarted();
void uiDebug(const char* cmd);

// ui_music.cpp
void musicBuild(lv_obj_t* tile);
void musicTick();
void musicFast();

// ui_eye.cpp
void eyeInit();
void eyeShow();
void eyeHide();
void eyeTick();
bool eyeActive();
bool eyeDismissed();
void eyeClearDismiss();
void eyeSetSleepy(bool s);
