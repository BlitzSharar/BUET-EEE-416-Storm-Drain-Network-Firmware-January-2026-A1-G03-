#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "config.h"
#include "lora_tx.h"
#include "beacon.h"

// Uplink packet, 9 bytes:
// node_id, seq, level hi, level lo, rate, rain, status, battery, checksum(XOR)
// RTC backed. Deep sleep restarts the sketch, so as an ordinary static this
// counter returned to 0 on every wake and every dry-state packet carried
// sequence 0. The gateway computes the gap as seq - (lastSeq + 1), which with
// both at 0 evaluates to 255, so it recorded 255 dropped packets for every
// single report and the packet delivery ratio, which is the whole reason this
// field exists, was meaningless. RTC memory survives sleep and is cleared only
// by a true power cycle, which is exactly when restarting the count is right.
RTC_DATA_ATTR static uint8_t seq = 0;

// Deliberately NOT RTC backed. setup() runs on every wake, including wakes
// from deep sleep, and calls lora_init(), which evaluates this fresh each
// time. Persisting it would only carry a stale verdict across a sleep.
static bool radioUp = false;

void lora_init() {
  radioUp = false;
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RESET, PIN_LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ_HZ)) {
    // Do NOT spin here. A board stuck in a while(true) produces no further
    // output, so the operator cannot tell a dead radio from a dead board, and
    // the watchdog then resets it in a loop that looks identical to a crash.
    // Report clearly, mark the radio down, and let the rest of the node run so
    // the sensors can still be observed on serial.
    Serial.println("*** LoRa init FAILED: check antenna, then NSS/RST/DIO0, then MISO/MOSI ***");
    Serial.flush();
    radioUp = false;
    return;
  }
  radioUp = true;
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
  randomSeed(esp_random() ^ (NODE_ID * 7919));
  // TODO week 5 range test: tune spreading factor and bandwidth from measured RSSI
}

// Randomised jitter before transmitting. Three nodes on identical periods will
// otherwise drift into a repeating collision and lose the same packet every
// cycle; jitter turns a systematic loss into an occasional one.
void lora_tx_jitter() {
  // Sliced rather than one long delay(). The jitter is up to TX_JITTER_MS, and
  // a single delay() of that length freezes the beacon and the buzzer mid
  // pattern: node.ino calls beacon_set() from the bottom of loop() and the pin
  // then holds whatever it last wrote. On the beacon node that reads as an LED
  // stuck on or off for three seconds, which is exactly the symptom the earlier
  // aliasing bug produced.
  unsigned long target = (unsigned long)random(0, (long)TX_JITTER_MS);
  unsigned long t0 = millis();
  while (millis() - t0 < target) {
    beacon_tick();
    delay(10);
  }
}

bool lora_send_report(int level, int rate, RainInput rain, const EngineOut& st, uint8_t batt) {
  // A failed radio is retried on the next scheduled report instead of staying
  // permanently down until the next reboot.
  if (!radioUp) lora_init();
  if (!radioUp) return false;

  int8_t r8 = (int8_t)constrain(rate, -127, 127);
  uint8_t p[9];
  p[0] = NODE_ID;
  p[1] = seq;
  p[2] = (uint8_t)((level >> 8) & 0xFF);
  p[3] = (uint8_t)(level & 0xFF);
  p[4] = (uint8_t)r8;
  p[5] = rain.raining ? (rain.mm_x10 ? rain.mm_x10 : 1) : 0;
  p[6] = engine_pack(st);          // low nibble severity, high nibble trend
  p[7] = batt;
  uint8_t c = 0; for (int i = 0; i < 8; i++) c ^= p[i];
  p[8] = c;

  if (!LoRa.beginPacket()) return false;
  if (LoRa.write(p, 9) != 9) return false;
  // Use the library's normal blocking completion path. The independent
  // sender sketch uses this exact call and completed hundreds of sends on the
  // installed hardware, while the former custom OP_MODE/IRQ polling path
  // repeatedly produced "left TX without TxDone" on that same board. The
  // project watchdog remains armed, so a genuinely stuck radio still causes a
  // controlled node restart instead of an indefinite silent lock-up.
  if (!LoRa.endPacket()) {
    radioUp = false;
    return false;
  }
  seq++;                              // packet completed on air
#if BRINGUP_MODE
  Serial.println("LoRa TX complete");
#endif
  return true;
}

bool lora_radio_up() { return radioUp; }
