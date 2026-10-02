#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "ui_local.h"

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);
static bool present = false;

void ui_init() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  // Not every board carries a display. The OLED is a bring-up and inspection
  // aid, not a requirement, so a node without one must run normally rather
  // than stalling on I2C writes that nobody will ever see.
  // Probe the bus. Adafruit_SSD1306::begin() does not detect a missing display,
  // so an address ACK is the real test. The early return matters here: the
  // splash below used to run unconditionally even after the check failed.
  Wire.beginTransmission(0x3C);
  present = (Wire.endTransmission() == 0);
  if (present) present = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!present) { Serial.println("no OLED found at 0x3C, continuing without it"); return; }
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
  oled.setCursor(0, 0); oled.print("Storm-Drain Gateway");
  oled.display();
}

void ui_show_last(const NodeReport& rep, BlockageVerdict verdict) {
  if (!present) return;
  oled.clearDisplay(); oled.setCursor(0, 0);
  oled.println("Gateway: last packet");
  oled.print("Node "); oled.print(rep.node_id);
  oled.print("  "); oled.println(gw_label(rep.severity, rep.trend));
  if (verdict != BLK_NONE) {
    oled.print("blockage "); oled.println(blockage_verdict_name(verdict));
  }
  oled.print(rep.level_mm); oled.println(" mm");
  oled.display();
}
