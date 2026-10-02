#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "config.h"
#include "lora_rx.h"

static uint8_t lastSeq[NUM_NODES + 1] = {0};
static bool seenAny[NUM_NODES + 1] = {false};
static uint32_t totalRx = 0, totalDrop = 0, totalReject = 0;

// WHY A FRAME WAS THROWN AWAY.
//
// Every rejection below used to be a bare `return false`. Six of them, all
// silent. A node flashed from a different build, a packet format that has moved
// on, a radio that is delivering nothing but noise, and a gateway with no
// antenna all produced exactly the same thing on the monitor: nothing. That is
// the single most expensive kind of silence on a bench, because the obvious
// conclusion is that no packet arrived, and the obvious fix is to go and rewire
// a link that was working the whole time.
//
// The counter always runs, so the heartbeat can show it even in a field build.
// The text is bring-up only and rate limited, because a radio sitting in band
// noise can reject continuously and would otherwise bury the lines that matter.
static void reject(const char* why) {
  totalReject++;
#if BRINGUP_MODE
  static unsigned long tSaid = 0;
  static const char* lastWhy = nullptr;
  unsigned long now = millis();
  // Say a NEW reason immediately. Repeat the same one at most every 2 s.
  if (why != lastWhy || now - tSaid >= 2000UL) {
    lastWhy = why;
    tSaid = now;
    Serial.printf("frame rejected: %s\n", why);
  }
#else
  (void)why;
#endif
}

void lora_gw_init() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RESET, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ_HZ)) { Serial.println("LoRa init failed"); while (true) delay(1000); }
  LoRa.setSyncWord(LORA_SYNC_WORD);
  // Hardware CRC. Without it the SX1276 hands up corrupted frames and the only
  // protection is our 8 bit XOR, which lets roughly 1 in 256 corruptions
  // through and is completely blind to bit flips aligned across bytes. A bad
  // frame that passes becomes a fake level reading in the archived dataset the
  // whole recession analysis rests on. The radio now discards them itself.
  LoRa.enableCrc();
  // Explicit spreading factor. Airtime roughly doubles per step, so this is the
  // main lever on both duty cycle and the reply window. Keep it at or below
  // LORA_SF_MAX and choose the lowest value that gives link margin in the
  // week 5 range test.
  LoRa.setSpreadingFactor(LORA_SF_DEFAULT);
  LoRa.receive();
}

bool lora_gw_poll(NodeReport* out) {
  int n = LoRa.parsePacket();
  // n == 0 is the ordinary idle case, not a rejection. Counting it would make
  // the reject total meaningless: it would climb by thousands a second on a
  // perfectly healthy gateway that simply has nothing to receive.
  if (n == 0) return false;
  if (n != 9) {
    reject("wrong length, so this is not one of our nodes or not this build");
    while (LoRa.available()) LoRa.read();
    return false;
  }
  // read() returns -1 on underrun; storing that as 0xFF could pass the XOR by
  // coincidence and inject a fabricated level reading into the archive.
  uint8_t p[9];
  for (int i = 0; i < 9; i++) {
    int v = LoRa.read();
    if (v < 0) { reject("short frame, the radio ran out mid packet"); return false; }
    p[i] = (uint8_t)v;
  }
  uint8_t c = 0; for (int i = 0; i < 8; i++) c ^= p[i];
  if (c != p[8]) { reject("XOR checksum mismatch, the frame is corrupt"); return false; }
  if (p[0] < 1 || p[0] > NUM_NODES) {
    reject("node id out of range, check NODE_ID and NUM_NODES");
    return false;
  }

  // Plausibility check. CRC and XOR make a corrupted frame unlikely but not
  // impossible, and a fabricated reading entering the archive would corrupt the
  // recession analysis silently. These bounds are physical, not arbitrary: the
  // sensor cannot measure beyond about 5 m and a percentage cannot exceed 100.
  uint16_t raw_lvl = (uint16_t)(((uint16_t)p[2] << 8) | (uint16_t)p[3]);
  int16_t lvl = (int16_t)raw_lvl;
  if (lvl < 0 || lvl > 5000) { reject("level outside 0 to 5000 mm"); return false; }
  // Status is now two packed fields. Both have to be in range, and a frame
  // that passed CRC and XOR but carries an impossible combination is still
  // a corrupted frame and must not reach the archive.
  if ((p[6] & 0x0F) > GW_SEV_WATERLOGGED) { reject("severity out of range"); return false; }
  if ((p[6] >> 4)   > GW_TR_STALLED)      { reject("trend out of range");    return false; }
  if (p[7] > 100) { reject("battery percent above 100"); return false; }

  out->node_id  = p[0];
  out->seq      = p[1];
  out->level_mm = lvl;
  out->rate     = (int8_t)p[4];
  out->rain     = p[5];
  out->status   = p[6];
  out->severity = (uint8_t)(p[6] & 0x0F);
  out->trend    = (uint8_t)(p[6] >> 4);
  out->blockage = (out->severity >= GW_SEV_ELEVATED) && (out->trend == GW_TR_STALLED);
  out->battery  = p[7];
  out->rssi     = (int16_t)LoRa.packetRssi();
  out->snr      = LoRa.packetSnr();
  out->at_ms    = (uint32_t)millis();

  uint8_t gap = 0;
  if (seenAny[p[0]]) gap = (uint8_t)(p[1] - (uint8_t)(lastSeq[p[0]] + 1));
  out->dropped = gap;
  totalRx++; totalDrop += gap;
  lastSeq[p[0]] = p[1];
  seenAny[p[0]] = true;

  return true;
}

// The node-1-only rain tracker that used to live here is gone. It existed when
// node 1 carried the only plate, and it made node 1 a single point of failure
// for the weather picture of the whole network. Every node reports its own
// plate in byte 5 of every packet now, gateway/blockage.cpp keeps the per-site
// view, and blockage_rain_believed() answers the same question from all of
// them plus the forecast. Keeping a second, narrower answer around was how the
// last few defects in this project started.
uint32_t rx_total_received() { return totalRx; }
uint32_t rx_total_dropped()  { return totalDrop; }
uint32_t rx_total_rejected() { return totalReject; }
