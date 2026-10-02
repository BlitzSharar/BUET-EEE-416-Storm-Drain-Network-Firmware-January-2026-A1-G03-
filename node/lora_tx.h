#ifndef LORA_TX_H
#define LORA_TX_H
#include "sensor_rain.h"
#include "status_engine.h"
void lora_init();

// False if the radio never came up. Callers must not block on a dead radio.
bool lora_radio_up();
void lora_tx_jitter();
// True only after an over-air attempt completed with the SX127x TxDone flag.
// Callers use this to avoid recording a failed attempt as a published state.
bool lora_send_report(int level_mm, int rate_mm_min, RainInput rain,
                      const EngineOut& st, uint8_t battery_pct);
#endif
