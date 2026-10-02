// TEST 2: OLED display.
// Wiring: OLED VCC->3.3V, GND->GND, SDA->GPIO21, SCL->GPIO22.
// Expect: "OLED OK" and a counter on the screen.
// Libraries needed: Adafruit SSD1306, Adafruit GFX (Library Manager).
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
int n = 0;
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found: check wiring and address (try 0x3D)");
    while (true) delay(1000);
  }
}
void loop() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE); oled.setTextSize(2);
  oled.setCursor(0, 0);  oled.println("OLED OK");
  oled.setCursor(0, 30); oled.println(n++);
  oled.display();
  delay(500);
}
