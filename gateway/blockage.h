#ifndef BLOCKAGE_H
#define BLOCKAGE_H
#include <stdint.h>
#include "lora_rx.h"

// Turning a measurement into a verdict.
//
// A node reports STALLED when water has been above LEVEL_HIGH_MM and has not
// receded by RECEDE_DROP_MM for RECEDE_WINDOW_MS. That is a measurement and it
// is all one sensor can honestly say. It does not distinguish a blocked drain
// from one being out-rained, because both look identical from a single level
// reading: water that is not going down.
//
// What separates them is rainfall, and the shape of the event across the
// network. Neither is visible to a node:
//
//   rainfall   every node carries a resistive plate and reports it in each
//              packet, and the gateway also fetches current precipitation from
//              open-meteo. Rain is local, so per site matters: a downpour over
//              the intersection says nothing about an outfall half a kilometre
//              away. A node that has gone silent gets no verdict at all rather
//              than a verdict based on somebody else's weather.
//   the shape  one drain stalled while its neighbours drain is a local fault.
//              Every drain stalled at once, in the rain, is a storm.
//
// Both of those live at the gateway, so the judgement lives here. The node got
// simpler and nothing depends on a broadcast arriving, which is what used to
// disable blockage detection silently on nodes 2 and 3.

enum BlockageVerdict : uint8_t {
  BLK_NONE      = 0,   // the node is not stalled
  BLK_STORM     = 1,   // stalled, but it is raining and the whole network is
                       // stalled together. That is a storm, not a fault.
  BLK_SUSPECTED = 2,   // stalled during rain while other nodes are draining, or
                       // stalled with no rainfall information available at all
  BLK_CONFIRMED = 3    // stalled with no rainfall to explain it
};

// Feed every accepted packet in. This keeps the per-node picture the verdict
// needs, including that node's own rain plate and when it was last wet.
void blockage_note_report(const NodeReport& rep);

// Call when a node is declared offline, so a stale stall from a node that has
// since gone silent stops counting towards the network picture.
void blockage_note_offline(uint8_t node_id);

// The verdict for one node, computed from the current picture.
BlockageVerdict blockage_verdict(uint8_t node_id);

// Why, in a few words, for the serial line, the archive and the report. Never
// null. This is the part that makes the decision defensible rather than magic.
const char* blockage_reason(uint8_t node_id);

const char* blockage_verdict_name(BlockageVerdict v);

// True while the gateway believes it is raining, from the plate or the forecast.
// Exposed so gateway.ino can show it and so the tests can drive it.
bool blockage_rain_believed();

// Test seam. The host suite has no radio and no clock, so it supplies both.
#ifdef BLOCKAGE_HOST_TEST
void blockage_test_reset();
void blockage_test_set_now(uint32_t ms);
void blockage_test_set_forecast(bool ok, uint16_t mm_x10);
#endif

#endif
