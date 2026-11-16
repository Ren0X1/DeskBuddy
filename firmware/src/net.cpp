// Wi-Fi (+ captive setup portal), PC link over UDP, OTA over HTTP and weather.
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <HTTPUpdateServer.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include "app.h"
#include "settings.h"
#include "webpanel.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef OTA_PASS
#define OTA_PASS "deskbuddy"
#endif

PcStats pc;
MediaInfo media;
Weather weather;
float vuTargetL = 0, vuTargetR = 0;
uint16_t artPixels[ART_SIZE * ART_SIZE];
volatile uint16_t artReadyId = 0;

static uint8_t artRx[ART_SIZE * ART_SIZE * 2];
static uint32_t artMask = 0;
static uint16_t artRxId = 0;

static WiFiUDP udp;
static IPAddress pcIp;
static uint16_t pcPort = PORT_PC;
static WebServer server(80);
static HTTPUpdateServer updater;
static DNSServer dns;
static Preferences wprefs;
static String ssid, pass;
static bool portalOn = false, everConnected = false, serverUp = false;
static volatile bool homeChanged = false;  // wakes the weather task
static bool cfgDirty = true;                // push settings to the PC agent asap
static uint32_t cfgSentAt = 0;

// older agents push their config.json home; only used until the panel sets one
static void onHome(JsonVariantConst h) {
  if (cfg.hasHome || h.isNull() || h["lat"].isNull()) return;
  JsonDocument d;
  d["home"].set(h);
  if (settingsFromJson(d.as<JsonVariantConst>())) {
    settingsSave();
    homeChanged = cfgDirty = true;
  }
}

static void applySettings(JsonVariantConst d) {
  float lat = cfg.lat, lon = cfg.lon;
  if (!d["screen"]["brightness"].isNull())
    hwSetUserBrightness(constrain(d["screen"]["brightness"].as<int>(), 10, 255), true);
  if (settingsFromJson(d)) {
    settingsSave();
    cfgDirty = true;
    if (lat != cfg.lat || lon != cfg.lon) homeChanged = true;
  }
}

bool pcOnline() { return pc.lastRx && millis() - pc.lastRx < 15000; }  // rides out Wi-Fi hiccups
bool pcAsleep() {  // no data from the PC app for cfg.pcOffSec (default 2 min)
  uint32_t lim = cfg.pcOffSec * 1000UL;
  return millis() > lim && (!pc.lastRx || millis() - pc.lastRx > lim);
}
const char* netSsid() { return ssid.c_str(); }

void netResetWifi() {
  wprefs.remove("ssid");
  wprefs.remove("pass");
  delay(200);
  ESP.restart();
}

void netSendCmd(const char* c) {
  if (!pcOnline()) return;
  char buf[48];
  snprintf(buf, sizeof buf, "{\"t\":\"cmd\",\"c\":\"%s\"}", c);
  udp.beginPacket(pcIp, pcPort);
  udp.print(buf);
  udp.endPacket();
}

// ------------------------------------------------------------------ UDP
// album art: 'A' 'R' id_lo id_hi chunk_idx chunk_count payload...
static void handleArt(const uint8_t* p, int n) {
  uint16_t id = p[2] | (p[3] << 8);
  uint8_t idx = p[4], total = p[5];
  int len = n - 6;
  if (total == 0 || total > 31 || idx >= total) return;
  if (id != artRxId) {
    artRxId = id;
    artMask = 0;
  }
  size_t off = (size_t)idx * ART_CHUNK;
  if (len <= 0 || off + len > sizeof artRx) return;
  memcpy(artRx + off, p + 6, len);
  artMask |= 1UL << idx;
  if (artMask == (1UL << total) - 1 && artReadyId != id) {
    memcpy(artPixels, artRx, sizeof artRx);
    artReadyId = id;
  }
}

static void onMedia(JsonDocument& d) {
  bool was = media.playing;
  media.has = d["has"] | false;
  media.playing = d["st"] | false;
  media.muted = d["mute"] | false;
  media.vol = d["vol"] | 0;
  media.pos = d["pos"] | 0.0f;
  media.dur = d["dur"] | 0.0f;
  media.accent = d["col"] | 0;
  media.artId = d["aid"] | 0;
  media.rxAt = millis();
  strlcpy(media.title, d["ti"] | "", sizeof media.title);
  strlcpy(media.artist, d["ar"] | "", sizeof media.artist);
  strlcpy(media.app, d["app"] | "", sizeof media.app);
  if (media.playing && !was) uiOnMediaStarted();
}

static void handleUdp() {
  static uint8_t pkt[1500];
  for (int len = udp.parsePacket(); len > 0; len = udp.parsePacket()) {
    int n = udp.read(pkt, sizeof(pkt) - 1);
    if (n <= 0) continue;
    pcIp = udp.remoteIP();
    pcPort = udp.remotePort();
    if (n > 6 && pkt[0] == 'A' && pkt[1] == 'R') {
      handleArt(pkt, n);
      continue;
    }
    if (n > 6 && pkt[0] == 'M' && pkt[1] == 'P') {
      alertOnMapChunk(pkt, n);
      continue;
    }
    pkt[n] = 0;
    JsonDocument d;
    if (deserializeJson(d, (const char*)pkt)) continue;
    const char* t = d["t"] | "";
    if (!strcmp(t, "vu")) {
      vuTargetL = max(vuTargetL, (float)(d["l"] | 0.0f));
      vuTargetR = max(vuTargetR, (float)(d["r"] | 0.0f));
    } else if (!strcmp(t, "stats")) {
      pc.cpu = d["cpu"] | 0.0f;
      pc.ram = d["ram"] | 0.0f;
      pc.gpu = d["gpu"] | 0.0f;
      pc.gpuTemp = d["gt"] | 0.0f;
      pc.vram = d["vram"] | 0.0f;
      pc.idle = d["idle"] | 0;
      strlcpy(pc.host, d["host"] | "PC", sizeof pc.host);
      strlcpy(pc.gpuName, d["gn"] | "", sizeof pc.gpuName);
      pc.lastRx = millis();
      onHome(d["home"]);
    } else if (!strcmp(t, "set")) {  // tray toggles from the agent
      applySettings(d.as<JsonVariantConst>());
    } else if (!strcmp(t, "media")) {
      onMedia(d);
    } else if (!strcmp(t, "radar")) {
      radarOnJson(d);
    } else if (!strcmp(t, "alert")) {
      alertOnJson(d);
    }
  }
}

static void sendCfg() {
  if (!pcOnline() || (!cfgDirty && millis() - cfgSentAt < 10000)) return;
  JsonDocument d;
  d["t"] = "cfg";
  settingsToJson(d);
  static char buf[900];
  size_t n = serializeJson(d, buf, sizeof buf);
  udp.beginPacket(pcIp, pcPort);
  udp.write((const uint8_t*)buf, n);
  udp.endPacket();
  cfgDirty = false;
  cfgSentAt = millis();
}

static void sendHello() {
  static uint32_t last = 0;
  if (pcOnline() || millis() - last < 2000) return;
  last = millis();
  IPAddress bc = WiFi.localIP();
  bc[3] = 255;
  udp.beginPacket(bc, PORT_PC);
  udp.print("{\"t\":\"hello\",\"name\":\"" HOSTNAME "\"}");
  udp.endPacket();
}

// ------------------------------------------------------------------ web: portal + OTA
static String esc(const String& s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '\'') o += "&#39;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static const char PAGE_HEAD[] PROGMEM =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
    "<title>DeskBuddy</title><style>body{font-family:system-ui;background:#000;color:#eee;"
    "max-width:360px;margin:40px auto;padding:0 16px}input,button{width:100%;padding:12px;"
    "margin:6px 0 16px;border-radius:12px;border:1px solid #333;background:#16181d;color:#eee;"
    "font-size:16px;box-sizing:border-box}button{background:#5ac8fa;color:#000;border:0;"
    "font-weight:600}small{color:#888}</style>";

static void pageRoot() {
  if (!portalOn) {
    server.send_P(200, "text/html; charset=utf-8", PANEL_HTML);
    return;
  }
  String h = FPSTR(PAGE_HEAD);
  h += "<h2>DeskBuddy Wi-Fi</h2><form method=post action=/save><label>Network</label>"
       "<input name=s list=n required autocomplete=off><datalist id=n>";
  int n = WiFi.scanComplete();
  for (int i = 0; i < n; i++) h += "<option value='" + esc(WiFi.SSID(i)) + "'>";
  h += "</datalist><label>Password</label><input name=p type=password>"
       "<button>Save &amp; connect</button></form>";
  server.send(200, "text/html", h);
}

static void pageSave() {
  if (!portalOn) {
    server.send(403, "text/plain", "portal closed");
    return;
  }
  wprefs.putString("ssid", server.arg("s"));
  wprefs.putString("pass", server.arg("p"));
  String h = FPSTR(PAGE_HEAD);
  h += "<h2>Saved &#10003;</h2><p>DeskBuddy is restarting and joining <b>" + esc(server.arg("s")) +
       "</b>.</p>";
  server.send(200, "text/html", h);
  delay(1000);
  ESP.restart();
}

static void pageNotFound() {
  if (portalOn) {  // captive portal: send every URL to the setup page
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "not found");
  }
}

static void apiConfigGet() {
  JsonDocument d;
  settingsToJson(d);
  d["screen"]["brightness"] = hwUserBrightness();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void apiConfigPost() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  applySettings(d.as<JsonVariantConst>());
  apiConfigGet();
}

static void apiStatus() {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["ssid"] = ssid;
  d["rssi"] = WiFi.RSSI();
  d["ip"] = WiFi.localIP().toString();
  d["pc"] = pcOnline();
  d["host"] = pc.host;
  uint32_t m = millis() / 60000;
  char up[16];
  snprintf(up, sizeof up, "%luh %02lum", m / 60, m % 60);
  d["up"] = up;
  d["heap"] = ESP.getFreeHeap();
  d["backlight"] = hwTarget();
  d["tile"] = uiTile();
  d["eyes"] = eyeActive();
  d["sad"] = eyeIsSad();
  d["tr"] = eyeIsTransition();
  d["cameo"] = eyeIsCameo();
  {
    int b, at;
    bool ms, mx, rs, rx;
    uiPolicyState(&b, &at, &ms, &mx, &rs, &rx);
    JsonObject m = d["mgr"].to<JsonObject>();
    m["base"] = b, m["auto"] = at, m["mus"] = ms, m["musNo"] = mx, m["rad"] = rs, m["radNo"] = rx;
  }
  d["alert"] = alertActive();
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void startServer() {
  if (serverUp) return;
  serverUp = true;
  updater.setup(&server, "/update", "deskbuddy", OTA_PASS);
  server.on("/", pageRoot);
  server.on("/save", HTTP_POST, pageSave);
  server.on("/api/config", HTTP_GET, apiConfigGet);
  server.on("/api/config", HTTP_POST, apiConfigPost);
  server.on("/api/status", HTTP_GET, apiStatus);
  server.on("/api/reboot", HTTP_POST, [] {
    server.send(200, "text/plain", "ok");
    delay(500);
    ESP.restart();
  });
  server.on("/api/wifireset", HTTP_POST, [] {
    server.send(200, "text/plain", "ok");
    delay(500);
    netResetWifi();
  });
  server.onNotFound(pageNotFound);
  server.begin();
}

static void startPortal() {
  portalOn = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME);
  dns.start(53, "*", WiFi.softAPIP());
  WiFi.scanNetworks(true);
  startServer();
  uiPortal(true);
  Serial.println("Setup portal: " AP_NAME);
}

static void stopPortal() {
  portalOn = false;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  uiPortal(false);
}

static void wifiWatch() {
  static bool was = false;
  static uint32_t lastTry = 0;
  bool now = WiFi.isConnected();
  if (now && !was) {
    everConnected = true;
    Serial.printf("WiFi OK: %s\n", WiFi.localIP().toString().c_str());
    if (portalOn) stopPortal();
    udp.begin(PORT_RX);
    MDNS.begin(HOSTNAME);
    MDNS.addService("http", "tcp", 80);
    startServer();
  }
  was = now;
  if (!now && ssid.length()) {
    // while the portal is open, only retry when nobody is using it (retries hop channels)
    uint32_t every = portalOn ? 60000 : 15000;
    if (millis() - lastTry > every && (!portalOn || WiFi.softAPgetStationNum() == 0)) {
      lastTry = millis();
      WiFi.begin(ssid.c_str(), pass.c_str());
    }
  }
  if (!everConnected && !portalOn && (!ssid.length() || millis() > 45000)) startPortal();
}

// ------------------------------------------------------------------ weather (own task)
static void weatherTask(void*) {
  float lat = NAN, lon = NAN;
#ifdef WEATHER_LAT
  lat = WEATHER_LAT;
  lon = WEATHER_LON;
#endif
  for (;;) {
    if (!WiFi.isConnected()) {
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    if (cfg.hasHome) {  // configured home (web panel) wins over everything
      lat = cfg.lat;
      lon = cfg.lon;
      homeChanged = false;
      if (cfg.city[0]) strlcpy(weather.city, cfg.city, sizeof weather.city);
    }
    if (isnan(lat)) {  // last resort: rough location from the public IP
      WiFiClient c;
      HTTPClient http;
      if (http.begin(c, "http://ip-api.com/json/?fields=status,lat,lon,city") && http.GET() == 200) {
        JsonDocument d;
        if (!deserializeJson(d, http.getString()) && d["status"] == "success") {
          lat = d["lat"];
          lon = d["lon"];
          strlcpy(weather.city, d["city"] | "", sizeof weather.city);
        }
      }
      http.end();
      if (isnan(lat)) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        continue;
      }
    }
    char url[256];
    snprintf(url, sizeof url,
             // plain HTTP on purpose: TLS needs ~40 KB of heap, which starves OTA uploads
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,is_day"
             "&daily=temperature_2m_max,temperature_2m_min&forecast_days=1&timezone=auto",
             lat, lon);
    WiFiClient wc;
    HTTPClient http;
    bool ok = false;
    if (http.begin(wc, url) && http.GET() == 200) {
      JsonDocument d;
      if (!deserializeJson(d, http.getString())) {
        weather.temp = d["current"]["temperature_2m"] | 0.0f;
        weather.code = d["current"]["weather_code"] | 0;
        weather.isDay = (d["current"]["is_day"] | 1) == 1;
        weather.tmax = d["daily"]["temperature_2m_max"][0] | 0.0f;
        weather.tmin = d["daily"]["temperature_2m_min"][0] | 0.0f;
        weather.valid = ok = true;
      }
    }
    http.end();
    // sleep until the next refresh, or until the PC tells us a new home
    uint32_t until = millis() + (ok ? 15 * 60000 : 60000);
    while ((int32_t)(until - millis()) > 0 && !homeChanged) vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ------------------------------------------------------------------ public
void netInit() {
  wprefs.begin("wifi", false);
  ssid = wprefs.getString("ssid", WIFI_SSID);
  pass = wprefs.getString("pass", WIFI_PASS);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);
  if (ssid.length()) WiFi.begin(ssid.c_str(), pass.c_str());
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");

  Update.onProgress([](size_t done, size_t) {
    int total = server.clientContentLength();
    uiOtaProgress(total > 0 ? min(100, (int)(done * 100 / total)) : 0);
  });
  xTaskCreate(weatherTask, "weather", 8192, NULL, 1, NULL);
}

void netLoop() {
  wifiWatch();
  if (portalOn) dns.processNextRequest();
  if (serverUp) server.handleClient();
  if (WiFi.isConnected()) {
    handleUdp();
    sendHello();
    sendCfg();
  }
}
