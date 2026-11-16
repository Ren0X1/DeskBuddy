#include "settings.h"
#include <Preferences.h>

Settings cfg;
static Preferences p;

void settingsLoad() {
  p.begin("cfg", true);
  size_t n = p.isKey("blob") ? p.getBytesLength("blob") : 0;
  if (n && n <= sizeof(Settings)) p.getBytes("blob", &cfg, n);  // older firmware saved a shorter prefix
  p.end();
  // fields added later may have landed in the old blob's padding: sanitise
  const Settings def;
  if (cfg.pcOffSec < 30 || cfg.pcOffSec > 3600) cfg.pcOffSec = def.pcOffSec;
  if (cfg.idleEyeSec < 30) cfg.idleEyeSec = def.idleEyeSec;
  if (cfg.sleepySec < 60) cfg.sleepySec = def.sleepySec;
  if (cfg.radiusKm < 2) cfg.radiusKm = def.radiusKm;
  if (cfg.cameoMin < 2 || cfg.cameoMin > 120) cfg.cameoMin = def.cameoMin;
  // migrate the home pushed by older agents (stored by net.cpp)
  if (!cfg.hasHome) {
    Preferences w;
    w.begin("wifi", true);
    if (w.isKey("hlat")) {
      cfg.lat = w.getFloat("hlat");
      cfg.lon = w.getFloat("hlon");
      strlcpy(cfg.city, w.getString("hcity", "").c_str(), sizeof cfg.city);
      cfg.hasHome = true;
    }
    w.end();
  }
}

void settingsSave() {
  p.begin("cfg", false);
  p.putBytes("blob", &cfg, sizeof cfg);
  p.end();
}

static void splitList(const char* csv, JsonArray out) {
  char buf[80];
  strlcpy(buf, csv, sizeof buf);
  for (char* t = strtok(buf, ", "); t; t = strtok(NULL, ", ")) out.add(t);
}

void settingsToJson(JsonDocument& d) {
  JsonObject h = d["home"].to<JsonObject>();
  h["lat"] = cfg.lat;
  h["lon"] = cfg.lon;
  h["city"] = cfg.city;
  h["set"] = cfg.hasHome;
  JsonObject r = d["radar"].to<JsonObject>();
  r["enabled"] = cfg.radarOn;
  r["radius_km"] = cfg.radiusKm;
  r["auto_show"] = cfg.autoShow;
  JsonObject s = d["spotlight"].to<JsonObject>();
  s["enabled"] = cfg.spotOn;
  s["show_seconds"] = cfg.showSec;
  s["repeat_minutes"] = cfg.repeatMin;
  splitList(cfg.watchCs, s["watch_callsigns"].to<JsonArray>());
  splitList(cfg.watchTypes, s["watch_types"].to<JsonArray>());
  JsonObject sc = d["screen"].to<JsonObject>();
  sc["night_start"] = cfg.nightStart;
  sc["night_end"] = cfg.nightEnd;
  sc["night_max"] = cfg.nightMax;
  sc["idle_eye_s"] = cfg.idleEyeSec;
  sc["sleepy_s"] = cfg.sleepySec;
  sc["eyes"] = cfg.eyesOn;
  sc["pc_off_s"] = cfg.pcOffSec;
  sc["cameos"] = cfg.cameos;
  sc["cameo_min"] = cfg.cameoMin;
}

template <typename T>
static bool upd(T& dst, JsonVariantConst v, T lo, T hi) {
  if (v.isNull()) return false;
  T x = v.as<T>();
  if (x < lo) x = lo;
  if (x > hi) x = hi;
  if (x == dst) return false;
  dst = x;
  return true;
}

static bool updB(bool& dst, JsonVariantConst v) {
  if (v.isNull() || v.as<bool>() == dst) return false;
  dst = v.as<bool>();
  return true;
}

static bool updS(char* dst, size_t n, JsonVariantConst v) {
  if (v.isNull()) return false;
  char buf[96] = "";
  if (v.is<JsonArrayConst>()) {
    for (JsonVariantConst e : v.as<JsonArrayConst>()) {
      if (buf[0]) strlcat(buf, ",", sizeof buf);
      strlcat(buf, e.as<const char*>() ? e.as<const char*>() : "", sizeof buf);
    }
  } else {
    strlcpy(buf, v.as<const char*>() ? v.as<const char*>() : "", sizeof buf);
  }
  if (!strncmp(dst, buf, n)) return false;
  strlcpy(dst, buf, n);
  return true;
}

bool settingsFromJson(JsonVariantConst d) {
  bool ch = false;
  JsonVariantConst h = d["home"];
  if (!h.isNull() && !h["lat"].isNull() && !h["lon"].isNull()) {
    ch |= upd(cfg.lat, h["lat"], -90.0f, 90.0f);
    ch |= upd(cfg.lon, h["lon"], -180.0f, 180.0f);
    ch |= updS(cfg.city, sizeof cfg.city, h["city"]);
    if (!cfg.hasHome) cfg.hasHome = ch = true;
  }
  JsonVariantConst r = d["radar"];
  ch |= updB(cfg.radarOn, r["enabled"]);
  ch |= upd<uint8_t>(cfg.radiusKm, r["radius_km"], 2, 100);
  ch |= updB(cfg.autoShow, r["auto_show"]);
  JsonVariantConst s = d["spotlight"];
  ch |= updB(cfg.spotOn, s["enabled"]);
  ch |= upd<uint16_t>(cfg.showSec, s["show_seconds"], 10, 600);
  ch |= upd<uint16_t>(cfg.repeatMin, s["repeat_minutes"], 5, 1440);
  ch |= updS(cfg.watchCs, sizeof cfg.watchCs, s["watch_callsigns"]);
  ch |= updS(cfg.watchTypes, sizeof cfg.watchTypes, s["watch_types"]);
  JsonVariantConst sc = d["screen"];
  ch |= upd<uint8_t>(cfg.nightStart, sc["night_start"], 0, 23);
  ch |= upd<uint8_t>(cfg.nightEnd, sc["night_end"], 0, 23);
  ch |= upd<uint8_t>(cfg.nightMax, sc["night_max"], 0, 255);
  ch |= upd<uint16_t>(cfg.idleEyeSec, sc["idle_eye_s"], 30, 3600);
  ch |= upd<uint16_t>(cfg.sleepySec, sc["sleepy_s"], 60, 7200);
  ch |= updB(cfg.eyesOn, sc["eyes"]);
  ch |= upd<uint16_t>(cfg.pcOffSec, sc["pc_off_s"], 30, 3600);
  ch |= updB(cfg.cameos, sc["cameos"]);
  ch |= upd<uint8_t>(cfg.cameoMin, sc["cameo_min"], 2, 120);
  return ch;
}
