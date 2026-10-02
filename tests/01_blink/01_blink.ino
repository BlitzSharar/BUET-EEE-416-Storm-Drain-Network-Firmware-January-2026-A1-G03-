// TEST 1: Blink. Proves the board, cable, driver, and IDE all work.
// Expect: the small blue LED on the ESP32 board blinks once per second,
// and the Serial Monitor (115200 baud) prints a counter.
int n = 0;
void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);   // onboard LED is GPIO2 on most DevKit v1 boards
}
void loop() {
  digitalWrite(2, HIGH); delay(500);
  digitalWrite(2, LOW);  delay(500);
  Serial.print("alive "); Serial.println(n++);
}
