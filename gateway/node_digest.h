#ifndef NODE_DIGEST_H
#define NODE_DIGEST_H
#include <stdint.h>
#include "lora_rx.h"
// config.h FIRST, so its DIGEST_SAMPLES wins. Without this include the fallback
// twenty lines down would define the value before config.h was ever read, and
// changing the number in config.h would silently do nothing to this file while
// appearing to work everywhere else.
#include "config.h"

// FIVE SAMPLES, ONE LINE.
//
// The monitor reports a node's condition once per DIGEST_SAMPLES packets rather
// than once per packet, and the condition it reports is the severity that
// arrived most often inside that group. This is a VOTE, not an arithmetic mean:
// severity is a category, and averaging 0 and 2 into 1 would invent a state
// nothing ever measured.
//
// The node stays the classifier. It already debounces severity over
// DEBOUNCE_SAMPLES before it transmits, and that same value drives its buzzer,
// its beacon and the road-unsafe interlock. If the gateway re-derived severity
// from an averaged level it would become a SECOND classifier with its own
// answer, and a node sounding a continuous tone while the gateway called the
// site partial would be the two of them disagreeing about one drain. So the
// gateway only counts what the node said.
//
// The level is a genuine mean, because a level IS a quantity and five of them
// average meaningfully. It is reported alongside the voted severity.

#ifndef DIGEST_SAMPLES
#define DIGEST_SAMPLES 5
#endif
#if DIGEST_SAMPLES < 1
#error "DIGEST_SAMPLES must be at least 1"
#endif

struct Digest {
  uint8_t  node_id;
  uint8_t  severity;     // the winner of the vote, GW_SEV_*
  int16_t  level_mm;     // mean of the group, rounded to nearest
  int8_t   rate;         // mean of the group, rounded toward zero
  bool     rain;         // wet if a majority of the group read wet
  uint8_t  samples;      // always DIGEST_SAMPLES, carried for the record
};

// Feed every accepted packet. Returns true exactly once per DIGEST_SAMPLES
// packets FROM THAT NODE, at which point *out holds the group's verdict.
//
// The count is per node and not global. Nodes report on their own cadence and
// a quiet node must not have its group closed early by a busy neighbour, which
// is what a shared counter would do.
bool digest_note(const NodeReport& rep, Digest* out);

// "partial blockage", "complete blockage", or an empty string for NORMAL.
// Empty is not an oversight. A normal node is not reported at all, so this
// returning "" is the caller's signal to print nothing.
const char* digest_words(uint8_t severity);

#ifdef DIGEST_HOST_TEST
void digest_test_reset();
#endif

#endif
