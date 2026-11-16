#pragma once
// Optional: copy to secrets.h (git-ignored) to bake credentials into a local build.
// Without it DeskBuddy opens the "DeskBuddy-Setup" Wi-Fi portal on first boot.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASS "your-wifi-password"
#define OTA_PASS "pick-a-password"  // must match firmware/secrets.ini
