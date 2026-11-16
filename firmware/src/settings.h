#pragma once
// User settings, editable from the web panel (http://deskbuddy.local) and kept in NVS.
// DeskBuddy is the source of truth: it pushes them to the PC agent (radar config).
#include <ArduinoJson.h>

struct Settings {
  // home
  bool hasHome = false;
  float lat = 40.4168f, lon = -3.7038f;
  char city[32] = "";
  // radar (applied by the PC agent)
  bool radarOn = true, autoShow = true;
  uint8_t radiusKm = 15;
  // spotlight alerts (applied by the PC agent)
  bool spotOn = true;
  uint16_t showSec = 60, repeatMin = 120;
  char watchCs[80] = "AF1,AF2,EXEC1F,SAM";
  char watchTypes[80] = "A337,A3ST,BLCF,A124,B52,U2";
  // screen
  uint8_t nightStart = 23, nightEnd = 8, nightMax = 35;
  uint16_t idleEyeSec = 120, sleepySec = 900;
  bool eyesOn = true;
  // appended fields keep older saved blobs loadable (prefix-compatible)
  uint16_t pcOffSec = 120;  // no data from the PC for this long -> backlight off
};

extern Settings cfg;
void settingsLoad();
void settingsSave();
void settingsToJson(JsonDocument& d);         // full config (web panel + agent sync)
bool settingsFromJson(JsonVariantConst d);   // partial update, returns true if anything changed
