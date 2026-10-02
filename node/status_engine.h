#ifndef STATUS_ENGINE_H
#define STATUS_ENGINE_H
#include <stdint.h>

// Two concurrent machines, because depth and direction are independent facts.
//
// The previous design carried both in one ordered enum, which made a receding
// state impossible to add: appending it would have made falling water the most
// severe value in the system, turning the beacon solid and forcing the fastest
// cadence exactly when the situation was improving. Splitting them costs one
// nibble on the wire and nothing anywhere else.

// SEVERITY. From depth alone, and ordered. Every safety decision reads this
// and only this, which is the property that guarantees a falling level can
// never switch a warning off.
enum Severity : uint8_t {
  SEV_NORMAL      = 0,   // level < LEVEL_HIGH_MM
  SEV_ELEVATED    = 1,   // LEVEL_HIGH_MM <= level < LEVEL_SAFE_MM
  SEV_WATERLOGGED = 2    // level >= LEVEL_SAFE_MM
};

// TREND. From motion alone, measured across a fixed WINDOW OF TIME rather than
// a count of samples. The sampling interval changes with state, so a deadband
// applied to a sample count would be thirty times more sensitive when the node
// is idle than when something is happening.
enum Trend : uint8_t {
  TR_STEADY   = 0,
  TR_RISING   = 1,
  TR_RECEDING = 2,
  TR_STALLED  = 3        // water up and not receding. NOT a blockage verdict.
};

// STALLED IS NOT BLOCKAGE, and keeping the two apart is the point of the split.
//
// A node can measure that water is not going down. It cannot measure why. A
// blocked drain and a drain being out-rained look identical from one sensor.
// What tells them apart is rainfall AT THAT SITE, and what the other drains are
// doing at the same moment. A node can now measure the first, since every node
// carries its own rain plate, but it can never see the second.
//
// So the node reports STALLED on level alone and takes no rain input at all,
// even though it has a plate. It reports its rainfall as a separate field and
// the gateway combines the two across all three sites. Gating the stall at the
// node would suppress it during rain, and the gateway would then never see the
// one case worth seeing: one drain stalled while its neighbours drain in the
// same downpour. See gateway/blockage.h.
//
// This also removes a failure that cost an afternoon: nodes 2 and 3 used to
// have no rain plate, so their blockage rule depended on a gateway broadcast
// arriving, and when it did not the rule was silently disabled and nothing
// said so.

// ATTENTION. Scheduling only: how much the site needs watching, which is not
// the same as how dangerous it is. Ordered so power.cpp can compare it.
// NEVER use this for a safety decision. Use engine_road_unsafe().
enum Attention : uint8_t {
  ATT_QUIET = 0,
  ATT_WATCH = 1,
  ATT_EVENT = 2
};

struct EngineOut {
  Severity severity;
  Trend    trend;
  bool     stalled;      // derived and debounced. A measurement, not a verdict.
};

EngineOut engine_update(int level_mm, int rate_mm_min);
EngineOut engine_current();

// Stall is a combination, not a state. Shallow water is never a stall however
// long it sits, because the recession rule only has meaning once the drain is
// holding water it should have shed.
inline bool engine_stalled(const EngineOut& e) {
  return e.severity >= SEV_ELEVATED && e.trend == TR_STALLED;
}

inline Attention engine_attention(const EngineOut& e) {
  if (e.stalled || e.severity >= SEV_WATERLOGGED) return ATT_EVENT;
  if (e.severity >= SEV_ELEVATED || e.trend == TR_RISING) return ATT_WATCH;
  return ATT_QUIET;
}

// Cadence reacts as soon as a more severe severity enters debounce. Otherwise a
// node can stay on the dry cadence, or asleep, while high water is still
// accumulating the samples it needs to publish.
Attention engine_sampling_attention();

// True when the most recent reading was at or above LEVEL_SAFE_MM, meaning the
// road is physically unsafe right now whatever has been published.
//
// Read from the raw level rather than the debounced severity on purpose. The
// debounce may still be publishing NORMAL while the water is over the road, and
// a stable state is allowed to deep sleep, which drops GPIO and would take the
// beacon with it. power_may_sleep() consults this.
bool engine_road_unsafe();

// Wire format. One byte, unchanged in size: the old status used two bits of it.
//   low nibble  severity 0..2
//   high nibble trend    0..3
inline uint8_t engine_pack(const EngineOut& e) {
  return (uint8_t)(((uint8_t)e.trend << 4) | (uint8_t)e.severity);
}
inline Severity engine_unpack_severity(uint8_t b) { return (Severity)(b & 0x0F); }
inline Trend    engine_unpack_trend(uint8_t b)    { return (Trend)(b >> 4); }
inline bool     engine_pack_valid(uint8_t b) {
  return (b & 0x0F) <= (uint8_t)SEV_WATERLOGGED && (b >> 4) <= (uint8_t)TR_STALLED;
}

// One label per severity and trend combination, for the OLED, the serial line
// and the dashboard. Twelve readouts from two small fields, against four before.
const char* engine_label(Severity s, Trend t);
inline const char* engine_label(const EngineOut& e) {
  return engine_label(e.severity, e.trend);
}

#endif
