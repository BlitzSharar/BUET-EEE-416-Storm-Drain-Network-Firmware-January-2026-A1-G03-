// TEST 3: JSN-SR04T waterproof ultrasonic.
// Wiring: sensor board 5V->VIN(5V), GND->GND, TRIG->GPIO25.
// ECHO must go through a divider because it is a 5V signal and the
// ESP32 pin is 3.3V only: ECHO -> 1k resistor -> GPIO34, and from
// GPIO34 a 2k resistor -> GND. (1k + 2k gives 5V x 2/3 = 3.3V.)
// Expect: a distance in mm printed twice a second. Point the round
// transducer at a wall or the floor; it cannot read closer than
// about 250 mm (its dead zone), which is US_MIN_VALID_MM in config.h.
//
// TRIGGER PULSE: 20 us, NOT 10. The 10 us figure comes from the HC-SR04
// datasheet and is copied into almost every tutorial online. The JSN-SR04T
// needs about 20 us, and at 10 us it returns intermittent or no echoes and
// looks exactly like a dead sensor or bad wiring. This sketch had the 10 us
// value while the real firmware had 20, which meant the first component test
// anyone runs could condemn a perfectly good sensor.
#define TRIG 25
#define ECHO 34
#define TRIG_PULSE_US 20
void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
}
void loop() {
  digitalWrite(TRIG, LOW);  delayMicroseconds(4);
  digitalWrite(TRIG, HIGH); delayMicroseconds(TRIG_PULSE_US);
  digitalWrite(TRIG, LOW);
  long us = pulseIn(ECHO, HIGH, 30000UL);
  if (us == 0) Serial.println("no echo (too close, or check wiring)");
  else { Serial.print(us * 0.1715); Serial.println(" mm"); }
  delay(500);
}
