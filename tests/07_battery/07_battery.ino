// TEST 6: battery voltage divider.
// Wiring: battery + -> 100k resistor -> GPIO35, and from GPIO35 a
// 100k resistor -> GND (battery - also to GND). Equal resistors halve
// the voltage so a 4.2V full cell reads as 2.1V, safe for the pin.
//
// CONVERSION: this uses analogReadMilliVolts, NOT raw/4095*3.3.
// The naive conversion is wrong on the ESP32: the ADC is non-linear,
// ignores roughly the first 0.21 V at the default attenuation, saturates
// near 3.1 V, and its internal reference varies from about 1.0 to 1.2 V
// between individual chips. analogReadMilliVolts applies the per-chip
// calibration burned into eFuse. This sketch used the naive form while
// node/power.cpp used the calibrated one, so the CAL value you tuned here
// was compensating for an error the real firmware does not have, and
// carrying it across made the battery percentage worse, not better.
//
// Expect: a voltage matching a multimeter reading of the cell, before
// you touch CAL at all. Adjust CAL only to trim residual resistor
// tolerance, and expect it to stay near 2.0.
#define VBAT 35
float CAL = 2.0;   // divider ratio: 100k / 100k = 2.0
void setup() { Serial.begin(115200); pinMode(VBAT, INPUT); }
void loop() {
  // Average eight reads: this ADC is noisy sample to sample.
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(VBAT);
  mv /= 8;
  float v = (mv / 1000.0) * CAL;
  Serial.print("pin "); Serial.print(mv); Serial.print(" mV   battery: ");
  Serial.print(v); Serial.println(" V");
  delay(1000);
}
