#include <Arduino.h>
#include <LoRa.h>
#include "config.h"
#include "lora_rx.h"
#include "lora_tx.h"
#include "sensor_rain.h"
#include "beacon.h"

// Downlink broadcast, 5 bytes: BCAST_ID, rain_flag, forecast_mm_x10 hi, lo, checksum(XOR)
static void parse(int n) {
  if (n != 5) { while (LoRa.available()) LoRa.read(); return; }
  // read() returns -1 if the FIFO underruns, which would be stored as 0xFF and
  // could coincidentally satisfy the checksum. Check availability explicitly.
  uint8_t p[5];
  for (int i = 0; i < 5; i++) {
    int v = LoRa.read();
    if (v < 0) return;                          // short frame, discard
    p[i] = (uint8_t)v;
  }
  uint8_t c = 0; for (int i = 0; i < 4; i++) c ^= p[i];
  if (p[0] != BCAST_ID || c != p[4]) return;
  uint16_t fc = ((uint16_t)p[2] << 8) | p[3];
  // Written as a comparison rather than min<uint16_t>(). Arduino cores define
  // min as a preprocessor macro on some versions, and a macro cannot take an
  // explicit template argument, so min<uint16_t>(a, b) is a compile error there.
  rain_set_broadcast(p[1] != 0, (uint8_t)(fc > 255u ? 255u : fc));
}

void lora_rx_listen() { if (lora_radio_up()) LoRa.receive(); }

void lora_rx_poll() {
  if (!lora_radio_up()) return;
  int n = LoRa.parsePacket();
  if (n > 0) parse(n);
}

void lora_rx_window(unsigned long ms) {
  if (!lora_radio_up()) return;
  unsigned long t0 = millis();
  // Drive the beacon and buzzer through the wait. This loop runs for
  // RX_WINDOW_MS with nothing else touching those pins, which is long enough
  // to hold a 2 Hz caution blink at one level for the whole window.
  while (millis() - t0 < ms) { lora_rx_poll(); beacon_tick(); yield(); }
}
