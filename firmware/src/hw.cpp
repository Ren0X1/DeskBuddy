// Display (GC9A01), touch (CST816D), backlight and the serial debug console.
#include <Preferences.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "app.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_CST816S _touch;

 public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 80000000;
      c.freq_read = 20000000;
      c.spi_3wire = true;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 6;
      c.pin_mosi = 7;
      c.pin_miso = -1;
      c.pin_dc = 2;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs = 10;
      c.pin_rst = -1;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 240;
      c.readable = false;
      c.invert = true;
      c.rgb_order = false;
      c.bus_shared = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl = 3;
      c.invert = false;
      c.freq = 44100;
      c.pwm_channel = 1;
      _light.config(c);
      _panel.setLight(&_light);
    }
    {
      auto c = _touch.config();
      c.x_min = 0;
      c.x_max = 239;
      c.y_min = 0;
      c.y_max = 239;
      c.pin_int = -1;  // poll over I2C: the CST816 INT line is only a pulse
      c.pin_rst = 1;
      c.bus_shared = false;
      c.i2c_port = 0;
      c.i2c_addr = 0x15;
      c.pin_sda = 4;
      c.pin_scl = 5;
      c.freq = 400000;
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX tft;
static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf1[240 * 24];
static lv_color_t buf2[240 * 24];
static bool capturing = false;
static uint32_t lastTouch = 0;
static Preferences prefs;
static uint8_t userBright = 200, targetBright = 200;
static float curBright = 0;

uint32_t lastTouchMs() { return lastTouch; }
uint8_t hwUserBrightness() { return userBright; }
void hwSetTarget(uint8_t b) { targetBright = b; }
void hwPoke() { lastTouch = millis(); }

void hwSetUserBrightness(uint8_t b, bool save) {
  userBright = b;
  if (save) prefs.putUChar("bright", b);
}

static void flushCb(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  if (capturing) {
    Serial.printf("RECT %d %d %u %u\n", a->x1, a->y1, w, h);
    Serial.write((const uint8_t*)c, w * h * 2);
  }
  tft.startWrite();
  tft.setAddrWindow(a->x1, a->y1, w, h);
  tft.writePixels((lgfx::rgb565_t*)&c->full, w * h);
  tft.endWrite();
  lv_disp_flush_ready(d);
}

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
    lastTouch = millis();
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void hwInit() {
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  prefs.begin("deskbuddy", false);
  userBright = targetBright = prefs.getUChar("bright", 200);

  tft.init();
  tft.setBrightness(0);
  tft.fillScreen(TFT_BLACK);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, buf1, buf2, 240 * 24);
  static lv_disp_drv_t dd;
  lv_disp_drv_init(&dd);
  dd.hor_res = 240;
  dd.ver_res = 240;
  dd.flush_cb = flushCb;
  dd.draw_buf = &drawBuf;
  lv_disp_drv_register(&dd);
  static lv_indev_drv_t id;
  lv_indev_drv_init(&id);
  id.type = LV_INDEV_TYPE_POINTER;
  id.read_cb = touchCb;
  lv_indev_drv_register(&id);
}

// "shot" dumps the framebuffer (tools/screenshot.py), anything else goes to uiDebug
static void handleSerial() {
  static char line[40];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      if (n < sizeof(line) - 1) line[n++] = ch;
      continue;
    }
    line[n] = 0;
    n = 0;
    if (!strcmp(line, "shot")) {
      Serial.println("SHOT 240 240");
      capturing = true;
      lv_obj_invalidate(lv_scr_act());
      lv_obj_invalidate(lv_layer_top());
      lv_refr_now(NULL);
      capturing = false;
      Serial.println("END");
    } else if (!strcmp(line, "mem")) {
      Serial.printf("heap free %u, min %u, largest %u, up %lus, reset reason %d, stack left %u\n",
                    ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), millis() / 1000,
                    (int)esp_reset_reason(), uxTaskGetStackHighWaterMark(NULL));
    } else if (!strcmp(line, "fps")) {
      uint32_t t0 = millis(), frames = 0;
      while (millis() - t0 < 2000) {
        uint32_t f0 = millis();
        lv_timer_handler();
        frames++;
        delay(2);
        (void)f0;
      }
      Serial.printf("loop %u it/2s\n", frames);
    } else if (!strcmp(line, "i2cscan")) {  // shares the touch bus (port 0, SDA 4 / SCL 5)
      Serial.print("i2c:");
      for (int a = 1; a < 127; a++)
        if (lgfx::i2c::readRegister8(0, a, 0, 100000).has_value()) Serial.printf(" 0x%02X", a);
      auto lvl = lgfx::i2c::readRegister8(0, 0x75, 0x78, 100000);  // IP5306_I2C battery level
      Serial.printf("\nip5306 0x78: %s %02X\n", lvl.has_value() ? "ok" : "no",
                    lvl.has_value() ? lvl.value() : 0);
    } else if (!strcmp(line, "wifireset")) {
      netResetWifi();
    } else if (line[0]) {
      uiDebug(line);
    }
  }
}

void hwLoop() {
  handleSerial();
  static uint32_t last = 0;
  if (millis() - last < 20) return;
  last = millis();
  // smooth backlight fades
  curBright += (targetBright - curBright) * 0.12f;
  if (fabsf(targetBright - curBright) < 0.6f) curBright = targetBright;
  tft.setBrightness((uint8_t)curBright);
}
