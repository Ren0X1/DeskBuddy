// DeskBuddy - ESP32-2424S012 (ESP32-C3 + GC9A01 240x240 round IPS + CST816D touch)
// https://github.com/Ren0X1/DeskBuddy
#include "app.h"
#include "settings.h"

void setup() {
  settingsLoad();
  hwInit();
  uiInit();
  eyeInit();
  static const char* const boot[] = {"Arranque", "Despertar"};  // power on
  eyeTransition(boot, 2);
  netInit();
}

void loop() {
  lv_timer_handler();
  hwLoop();
  netLoop();
  delay(2);
}
