// TEST 4a: LoRa SENDER. Flash this to the board that will be a NODE.
// IMPORTANT: screw/solder the antenna on BEFORE powering the Ra-02.
// Transmitting without an antenna can permanently damage the radio chip.
// Wiring (Ra-02 -> ESP32): VCC->3.3V (NOT 5V), GND->GND, SCK->18,
// MISO->19, MOSI->23, NSS->5, RST->14, DIO0->26.
// Library needed: "LoRa" by Sandeep Mistry (Library Manager).
// Expect: "sent N" on this board's Serial Monitor once per second.
#include <SPI.h>
#include <LoRa.h>
int n = 0;
void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, 5);
  LoRa.setPins(5, 14, 26);
  if (!LoRa.begin(433E6)) { Serial.println("LoRa init failed: check wiring"); while (true) delay(1000); }
  LoRa.setSyncWord(0x37);
  // Match the production firmware. The library defaults to SF7 with the
  // hardware CRC off, while node/config.h runs SF8 with CRC on. Range and RSSI
  // measured at library defaults do not describe the link you actually deploy,
  // and airtime roughly doubles per spreading factor step, so the range table
  // has to be taken at the setting you will use. Change both sketches together
  // if you retune the spreading factor after the range test.
  LoRa.setSpreadingFactor(8);
  LoRa.enableCrc();
  Serial.println("sender ready (SF8, CRC on, sync 0x37)");
}
void loop() {
  LoRa.beginPacket();
  LoRa.print("hello ");
  LoRa.print(n);
  LoRa.endPacket();
  Serial.print("sent "); Serial.println(n++);
  delay(1000);
}
