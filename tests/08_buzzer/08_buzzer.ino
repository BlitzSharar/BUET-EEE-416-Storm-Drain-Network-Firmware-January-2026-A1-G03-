// TEST 8: buzzer. Run this when the node reaches WATERLOGGED and stays silent.
//
// Wiring: buzzer + to GPIO4, buzzer - to GND. If it is a module with three
// pins (VCC, GND, I/O) then VCC to 3V3, GND to GND, I/O to GPIO4.
//
// This sketch does ONE thing the firmware cannot do for you: it drives the pin
// two different ways, ten seconds apart, and tells you which one you are
// hearing. That answers the only question worth asking first.
//
// THE TWO KINDS OF BUZZER, which look identical and are not:
//
//   ACTIVE buzzer  has an oscillator inside. Apply a steady voltage and it
//                  makes a tone. digitalWrite(HIGH) is all it needs. Usually
//                  taller, often with a sticker over the hole, and it clicks
//                  once if you touch it briefly to 3V3.
//
//   PASSIVE piezo  is just a ceramic disc. It needs an ALTERNATING signal at an
//                  audible frequency. Steady DC makes it flex once and stop, so
//                  digitalWrite(HIGH) produces a faint tick and then nothing.
//                  Usually flatter, with the disc visible underneath.
//
// The firmware ships driving it the active way. If PHASE B is the one you can
// hear, set BUZZER_PASSIVE to 1 in node/config.h and reflash.
#define BUZZER 4
#define TONE_HZ 2700        // near the resonant peak of most small piezos
#define LEDC_CH 0

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);
  Serial.println();
  Serial.println("=== BUZZER TEST, GPIO4 ===");
  Serial.println("Listen carefully. Each phase runs for 10 seconds.");
  Serial.println();
}

void loop() {
  // ---------------- PHASE A: steady level, what an ACTIVE buzzer wants ------
  Serial.println("PHASE A: steady DC (active buzzer). Two chirps every 2 s.");
  Serial.println("   an ACTIVE buzzer sounds here. A PASSIVE one ticks faintly or is silent.");
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER, HIGH); delay(150); digitalWrite(BUZZER, LOW); delay(150);
    digitalWrite(BUZZER, HIGH); delay(150); digitalWrite(BUZZER, LOW); delay(1550);
  }

  Serial.println();
  // ---------------- PHASE B: square wave, what a PASSIVE piezo wants --------
  Serial.println("PHASE B: 2700 Hz square wave (passive piezo). Two chirps every 2 s.");
  Serial.println("   a PASSIVE piezo sounds here. An ACTIVE one may buzz oddly or stay quiet.");
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER, TONE_HZ, 8);
#else
  ledcSetup(LEDC_CH, TONE_HZ, 8);
  ledcAttachPin(BUZZER, LEDC_CH);
#endif
  for (int i = 0; i < 5; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(BUZZER, 128); delay(150); ledcWrite(BUZZER, 0); delay(150);
    ledcWrite(BUZZER, 128); delay(150); ledcWrite(BUZZER, 0); delay(1550);
#else
    ledcWrite(LEDC_CH, 128); delay(150); ledcWrite(LEDC_CH, 0); delay(150);
    ledcWrite(LEDC_CH, 128); delay(150); ledcWrite(LEDC_CH, 0); delay(1550);
#endif
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(BUZZER);
#else
  ledcDetachPin(BUZZER);
#endif
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  Serial.println();
  // ---------------- PHASE C: continuous, in case the chirps are too short ---
  Serial.println("PHASE C: 3 s continuous, both ways, in case 150 ms is too short to hear.");
  Serial.println("   steady DC for 3 s ...");
  digitalWrite(BUZZER, HIGH); delay(3000); digitalWrite(BUZZER, LOW); delay(500);
  Serial.println("   2700 Hz for 3 s ...");
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER, TONE_HZ, 8); ledcWrite(BUZZER, 128); delay(3000);
  ledcWrite(BUZZER, 0); ledcDetach(BUZZER);
#else
  ledcSetup(LEDC_CH, TONE_HZ, 8); ledcAttachPin(BUZZER, LEDC_CH);
  ledcWrite(LEDC_CH, 128); delay(3000);
  ledcWrite(LEDC_CH, 0); ledcDetachPin(BUZZER);
#endif
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  Serial.println();
  Serial.println("--- what you just heard tells you what to do ---");
  Serial.println("  heard PHASE A  -> buzzer is ACTIVE. Leave BUZZER_PASSIVE at 0.");
  Serial.println("                    If the firmware is still silent, the node never");
  Serial.println("                    reached sev=2. Check the serial sample line.");
  Serial.println("  heard PHASE B  -> buzzer is PASSIVE. Set BUZZER_PASSIVE to 1 in");
  Serial.println("                    node/config.h and reflash the node.");
  Serial.println("  heard NEITHER  -> wiring, or the buzzer needs more current than a");
  Serial.println("                    GPIO can source. Check continuity, try the pin");
  Serial.println("                    straight to 3V3, and use a transistor if it is a");
  Serial.println("                    bare magnetic buzzer rather than a module.");
  Serial.println();
  delay(3000);
}
