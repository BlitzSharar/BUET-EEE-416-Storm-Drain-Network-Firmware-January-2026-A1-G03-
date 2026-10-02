// TEST 4b: LoRa RECEIVER. Flash this to the board that will be the GATEWAY.
// Same wiring and antenna warning as the sender.
// Expect: "got: hello N (RSSI -xx)" on this board's Serial Monitor
// once per second while the sender board is running.
// If this works, the hardest part of the whole project works.
#include <SPI.h>
#include <LoRa.h>
void setup() {
  Serial.begin(115200);
  SPI.begin(18, 19, 23, 5);
  LoRa.setPins(5, 14, 26);
  if (!LoRa.begin(433E6)) { Serial.println("LoRa init failed: check wiring"); while (true) delay(1000); }
  LoRa.setSyncWord(0x37);
  // Must match the sender exactly. A different frequency, sync word or
  // spreading factor on either side is the classic "sender says sent, receiver
  // hears nothing" fault, and it looks identical to bad wiring.
  LoRa.setSpreadingFactor(8);
  LoRa.enableCrc();
  Serial.println("receiver ready (SF8, CRC on, sync 0x37)");
}
void loop() {
  int n = LoRa.parsePacket();
  if (n > 0) {
    Serial.print("got: ");
    while (LoRa.available()) Serial.print((char)LoRa.read());
    Serial.print("  (RSSI "); Serial.print(LoRa.packetRssi()); Serial.println(")");
  }
}
