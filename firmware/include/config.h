#pragma once
// ---- user tweakables ------------------------------------------------------

#ifndef FW_VERSION
#define FW_VERSION "dev"  // CI injects the git tag
#endif

#define TZ_INFO "CET-1CEST,M3.5.0,M10.5.0/3"  // Europe/Madrid (POSIX TZ string)

#define NIGHT_START_HOUR 23       // night mode window (local time)
#define NIGHT_END_HOUR 8
#define NIGHT_MAX_BRIGHTNESS 35   // 0-255 cap while it's night
#define PC_OFF_BRIGHTNESS 25      // daytime level when the PC is off/asleep
#define WAKE_ON_TOUCH_MS 20000    // a touch wakes a dark screen for this long

#define IDLE_EYE_SECONDS 120      // PC idle time before the eye shows up
#define IDLE_SLEEPY_SECONDS 900   // ...and when it gets sleepy

#define AP_NAME "DeskBuddy-Setup" // Wi-Fi setup portal
#define HOSTNAME "deskbuddy"      // -> deskbuddy.local

// Weather location: leave undefined to auto-detect from your public IP
// #define WEATHER_LAT 40.4168
// #define WEATHER_LON -3.7038

// ---- protocol (must match agent/deskbuddy_agent.py) -----------------------
#define PORT_RX 4210              // PC -> DeskBuddy
#define PORT_PC 4211              // DeskBuddy -> PC
#define ART_SIZE 80               // album art is ART_SIZE x ART_SIZE RGB565
#define ART_CHUNK 1280            // bytes per UDP chunk
