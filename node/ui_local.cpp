#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "ui_local.h"

static Adafruit_SSD1306 oled(128, 64, &Wire, -1);

static bool isOn = true;
static bool present = false;

void ui_init() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  // Explicit bus timeout. With no display fitted the lines float, and the
  // default behaviour on a stalled bus is long enough to matter during init.
  Wire.setTimeOut(50);
  // Not every board carries a display. The OLED is a bring-up and inspection
  // aid, not a requirement, so a node without one must run normally rather
  // than stalling on I2C writes that nobody will ever see.
  //
  // Probe the bus first. Adafruit_SSD1306::begin() does not check whether a
  // display is actually there: it fails only if the frame buffer cannot be
  // allocated, so it returns true on a board with no OLED at all and the
  // "continuing without it" path never ran. An address ACK is the real test.
  Wire.beginTransmission(0x3C);
  present = (Wire.endTransmission() == 0);
  if (present) present = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!present) Serial.println("no OLED found at 0x3C, continuing without it");
  if (present) {
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
  }
  isOn = true;
}

void ui_splash() {
  if (!present) return;
  oled.clearDisplay(); oled.setTextColor(SSD1306_WHITE); oled.setTextSize(1);
  oled.setCursor(0, 0); oled.print("Storm-Drain Node "); oled.print(NODE_ID);
  oled.display();
}

// Turning the panel off drops roughly 15 mA, which dominates the node budget
// in the dry state. The controller stays initialised so waking is immediate.
void ui_set_power(bool on) {
  if (!present) return;
  if (on == isOn) return;
  oled.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  isOn = on;
}

void ui_show(int level, int rate, const EngineOut& st, uint8_t batt) {
  if (!present) return;
  if (!isOn) return;
  oled.clearDisplay(); oled.setCursor(0, 0);
  oled.print("N"); oled.print(NODE_ID); oled.print(" ");
  oled.println(engine_label(st));   // severity and trend in one word
  oled.print("Level "); oled.print(level); oled.println(" mm");
  oled.print("Rate  "); oled.print(rate);  oled.println(" mm/min");
  oled.print("Batt  "); oled.print(batt);  oled.println(" %");
  oled.display();
}
