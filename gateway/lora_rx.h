#ifndef LORA_RX_H
#define LORA_RX_H
#include <stdint.h>

// Mirrors node/status_engine.h. The gateway never runs the engine, so it keeps
// its own copy of the wire encoding rather than including node headers.
//   status byte:  low nibble severity 0..2, high nibble trend 0..3
enum GwSeverity : uint8_t { GW_SEV_NORMAL = 0, GW_SEV_ELEVATED = 1, GW_SEV_WATERLOGGED = 2 };
enum GwTrend    : uint8_t { GW_TR_STEADY = 0, GW_TR_RISING = 1, GW_TR_RECEDING = 2, GW_TR_STALLED = 3 };

struct NodeReport {
  uint8_t node_id, seq, rain, status, battery;
  uint8_t severity;               // unpacked from status, low nibble
  uint8_t trend;                  // unpacked from status, high nibble
  bool    blockage;               // the node's STALL measurement, not a verdict.
                                  // gateway/blockage.h turns it into a verdict.
  int16_t level_mm; int8_t rate;
  int16_t rssi; float snr;        // link quality, recorded for the range study
  uint8_t dropped;                // packets missed since the previous sequence
  uint32_t at_ms;
};
void lora_gw_init();
bool lora_gw_poll(NodeReport* out);
// One label per severity and trend combination, matching the node's own table.
const char* gw_label(uint8_t severity, uint8_t trend);
uint32_t rx_total_received();
uint32_t rx_total_dropped();
// Frames that arrived on the air and were thrown away. A gateway that is
// receiving nothing and one that is receiving the wrong thing look identical
// without this. Counted in every build; the reason text is bring-up only.
uint32_t rx_total_rejected();
#endif
