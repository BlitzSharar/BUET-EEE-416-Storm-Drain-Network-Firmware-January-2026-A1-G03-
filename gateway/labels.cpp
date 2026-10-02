#include <stdint.h>
#include "lora_rx.h"

// Same eleven readouts the node shows on its own OLED.
//
// This is a SECOND COPY of node/status_engine.cpp engine_label(), because the
// node and the gateway are separate sketches that share no translation unit.
// The two had already drifted on three of the twelve combinations: the gateway
// still said NORMAL where the node said RISING, and still said BLOCKAGE where
// the node said STALLED. An operator at the node and one at the gateway were
// reading different words off the same byte, and the archive recorded the
// gateway's word, which asserted a blockage the verdict column denied.
//
// tests_host/label_test.cpp compiles BOTH tables and compares all twelve
// combinations, so they cannot drift again without a test failing. The grid
// itself is documented above engine_label() in node/status_engine.cpp.
const char* gw_label(uint8_t severity, uint8_t trend) {
  switch (severity) {
    case GW_SEV_NORMAL:
      if (trend == GW_TR_RISING)   return "NORMAL RISING";
      if (trend == GW_TR_RECEDING) return "NORMAL CLEARING";
      return "NORMAL";
    case GW_SEV_ELEVATED:
      if (trend == GW_TR_STALLED)  return "ELEVATED BLOCKED";
      if (trend == GW_TR_RISING)   return "ELEVATED RISING";
      if (trend == GW_TR_RECEDING) return "ELEVATED CLEARING";
      return "ELEVATED";
    default:
      if (trend == GW_TR_STALLED)  return "WATERLOG BLOCKED";
      if (trend == GW_TR_RISING)   return "WATERLOG RISING";
      if (trend == GW_TR_RECEDING) return "WATERLOG CLEARING";
      return "WATERLOG";
  }
}
