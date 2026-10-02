#include <Arduino.h>
#include <LoRa.h>
#include "config.h"
#include "lora_bcast.h"

// Downlink broadcast, 5 bytes: BCAST_ID, rain_flag, forecast hi, forecast lo, checksum(XOR)
void bcast_send(bool raining, uint16_t fc) {
  uint8_t p[5];
  p[0] = BCAST_ID;
  p[1] = raining ? 1 : 0;
  p[2] = (uint8_t)((fc >> 8) & 0xFF);
  p[3] = (uint8_t)(fc & 0xFF);
  uint8_t c = 0; for (int i = 0; i < 4; i++) c ^= p[i];
  p[4] = c;
  LoRa.beginPacket(); LoRa.write(p, 5); LoRa.endPacket();
  LoRa.receive();   // back to listening for node packets
}
