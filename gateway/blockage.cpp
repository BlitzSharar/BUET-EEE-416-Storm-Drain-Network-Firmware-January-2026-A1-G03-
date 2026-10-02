#include <stdio.h>          // snprintf, for the reason strings
#include "blockage.h"
#include "config.h"

#include "forecast.h"

// THE FORECAST IS ASKED TWO SEPARATE QUESTIONS AND THEY ARE NOT THE SAME ONE.
//
//   fc_wet()   the forecast says it is raining. Evidence FOR rain.
//   fc_have()  there is a fresh forecast at all. Evidence that dryness is
//              corroborated by something other than a single plate.
//
// Both go through forecast.cpp, which now expires a forecast after
// FORECAST_STALE_MS. Before that, one successful fetch was believed for the
// rest of the gateway's life: a stale WET reading made CONFIRMED unreachable
// and a stale DRY reading let one lone plate confirm a blockage, both in
// silence, both for ever.
#ifdef BLOCKAGE_HOST_TEST
  // Host build: the suite supplies the clock and the forecast. The WET
  // threshold is not re-implemented here. It goes through forecast_convert(),
  // the same function forecast.cpp calls, so the host cannot disagree with the
  // target about what counts as raining.
  static uint32_t g_now = 0;
  static bool     g_fc_ok = false;
  static uint16_t g_fc_mm_x10 = 0;
  static bool     g_fc_wet = false;
  static uint32_t now_ms()  { return g_now; }
  static bool     fc_wet()  { return g_fc_wet; }
  static bool     fc_have() { return g_fc_ok; }
  void blockage_test_set_now(uint32_t ms) { g_now = ms; }
  void blockage_test_set_forecast(bool ok, uint16_t mm_x10) {
    g_fc_ok = ok; g_fc_mm_x10 = mm_x10;
    uint16_t x = 0; bool r = false;
    forecast_convert((float)mm_x10 / 10.0f, &x, &r);
    g_fc_wet = ok && r;
  }
#else
  #include <Arduino.h>
  static uint32_t now_ms()  { return (uint32_t)millis(); }
  static bool     fc_wet()  { return forecast_rain_now(); }
  static bool     fc_have() { return forecast_ok(); }
#endif

struct NodeView {
  bool     seen       = false;
  bool     online     = false;
  bool     stalled    = false;
  uint8_t  severity   = 0;
  uint8_t  trend      = 0;
  uint32_t at_ms      = 0;
  bool     rainWet    = false;   // this node's own plate, right now
  bool     rainEver   = false;
  uint32_t rainLastWet = 0;
};
static NodeView view[NUM_NODES + 1];

// Every node carries its own plate now, so rainfall is per site rather than one
// reading standing in for all three. A site whose own node has gone quiet falls
// back to the whole-network picture, not to one nominated node.
//
// A drain legitimately takes time to clear once rain stops, so a recent wet
// reading keeps counting for RAIN_MEMORY_MS rather than being dropped the
// moment the last drop falls.

static char reason[NUM_NODES + 1][56];

static bool valid(uint8_t id) { return id >= 1 && id <= NUM_NODES; }

void blockage_note_report(const NodeReport& rep) {
  if (!valid(rep.node_id)) return;
  NodeView& v = view[rep.node_id];
  v.seen = true; v.online = true;
  v.stalled  = rep.blockage;          // the node's debounced stall measurement
  v.severity = rep.severity;
  v.trend    = rep.trend;
  v.at_ms    = rep.at_ms;

  v.rainWet = (rep.rain != 0);
  if (v.rainWet) {
    v.rainEver = true;
    v.rainLastWet = rep.at_ms;
  } else if (v.rainEver &&
             (uint32_t)(rep.at_ms - v.rainLastWet) > (uint32_t)RAIN_MEMORY_MS) {
    // Retire the memory once it has expired, rather than leaving a timestamp
    // lying around to be re-read for ever.
    //
    // The comparison is unsigned and wrap safe for ordinary spans, but a
    // timestamp left in place for 49.7 days comes back around: once millis()
    // wraps past it the difference returns to near zero and the 30 minute rain
    // memory re-arms itself from a reading taken seven weeks earlier. That is
    // the direction that suppresses a real blockage. Reports arrive far more
    // often than the rollover, so clearing the flag the first time it expires
    // means there is nothing left to come back around.
    v.rainEver = false;
    v.rainLastWet = 0;
  }
}

void blockage_note_offline(uint8_t node_id) {
  if (!valid(node_id)) return;
  view[node_id].online = false;
  // Nothing else needs clearing, and the two lines that used to clear stalled
  // and rainWet here have been removed rather than left as reassurance. Every
  // reader of this struct checks online first: node_wet(), rain_at(),
  // count_nodes() and blockage_verdict() all skip an offline node before
  // looking at anything else, and a node that comes back sets both fields from
  // its own packet. Mutating those two lines changed no behaviour at all,
  // which is what proved them dead. Defensive code that cannot execute is the
  // worst kind, because it reads as a guarantee that something is enforced.
}

// Has this particular site seen rain recently? Its own plate first, because a
// downpour is local, then the forecast, then the dead-plate override below.
// ONE definition of "this node counts as wet", because there used to be two.
//
// rain_at() asked "plate wet, or wet within RAIN_MEMORY_MS".
// dryness_corroborated() asked only "plate wet right now".
//
// So a neighbour whose plate dried five minutes ago was simultaneously wet
// enough for the network to be told it was raining and dry enough to corroborate
// a blockage. The guard that exists specifically to stop one corroded plate
// raising a callout was satisfied by a node the system itself still counted as
// rained on. The two questions now share this function and cannot drift.
static bool node_wet(uint8_t i) {
  const NodeView& v = view[i];
  if (!v.online) return false;
  if (v.rainWet) return true;
  return v.rainEver &&
         (uint32_t)(now_ms() - v.rainLastWet) <= (uint32_t)RAIN_MEMORY_MS;
}

// Only ever called for a node that is online and stalled: blockage_verdict()
// returns BLK_NONE before reaching here otherwise, and nothing else calls this.
// The offline fallback that used to sit at the top of this function was
// unreachable, and reachable or not it read as a guarantee that a silent node
// falls back to the network picture. Two different rewrites of it, one
// returning always-wet and one always-dry, both left the whole suite green,
// which is what proved it dead. Same principle as blockage_note_offline().
static bool rain_at(uint8_t id) {
  if (node_wet(id)) return true;
  if (fc_wet()) return true;

  // Its own plate says dry. Normally that settles it, and deliberately so: a
  // downpour over the intersection is not evidence about an outfall half a
  // kilometre away, which is the whole reason for fitting a plate per node.
  //
  // One exception. A resistive plate that has corroded open reads dry for ever,
  // and a dry plate now confirms a blockage by itself, so a failed plate would
  // manufacture alerts during every storm. If every OTHER reporting node says
  // wet, a single dissenting dry reading is more likely a dead plate than a dry
  // pocket. It takes at least two of them to outvote it: one neighbour
  // disagreeing is exactly the local variation this design exists to respect.
  int others = 0, wetOthers = 0;
  for (uint8_t i = 1; i <= NUM_NODES; i++) {
    if (i == id || !view[i].online) continue;
    others++;
    if (node_wet(i)) wetOthers++;
  }
  return others >= 2 && wetOthers == others;
}

// Anywhere on the network, for the display and as the fallback for a site whose
// own node is silent.
// Unsigned subtraction is already wrap safe for ordinary spans. An
// n >= rainLastWet guard here would BREAK it, dropping the rain memory in the
// false alarm direction. The one case it does not cover, a timestamp left
// sitting for a full 49.7 day rollover, is handled where it belongs: the
// memory is retired in blockage_note_report() the first time it expires, so
// there is nothing left to come back around.
bool blockage_rain_believed() {
  for (uint8_t i = 1; i <= NUM_NODES; i++) if (node_wet(i)) return true;
  return fc_wet();
}

// Is the dryness CORROBORATED, or is one plate saying it on its own?
//
// This used to ask "is any node online", which was a fair proxy for "do we have
// rainfall evidence" back when only node 1 carried a plate. Every node has one
// now, so the stalled node satisfies that test by itself and the branch became
// unreachable: a single dry reading was enough to CONFIRM a blockage.
//
// That matters because the failure is silent. A resistive plate that has
// corroded open reads dry for ever, and RAIN_WET_ADC is set by hand per plate,
// so one bad plate or one bad threshold would manufacture a named-drain
// maintenance callout during every storm.
//
// Confirmation therefore needs a second opinion: a working forecast, or at
// least one other reporting node that also says dry. With neither, the verdict
// stays SUSPECTED, which still raises the alert without claiming certainty.
static bool dryness_corroborated(uint8_t id) {
  if (fc_have()) return true;
  for (uint8_t i = 1; i <= NUM_NODES; i++) {
    if (i == id || !view[i].online) continue;
    if (!node_wet(i)) return true;       // same definition rain_at() uses
  }
  return false;
}

static void count_nodes(int& reporting, int& stalledCount) {
  reporting = 0; stalledCount = 0;
  for (uint8_t i = 1; i <= NUM_NODES; i++) {
    if (!view[i].seen || !view[i].online) continue;
    reporting++;
    if (view[i].stalled) stalledCount++;
  }
}

BlockageVerdict blockage_verdict(uint8_t node_id) {
  if (!valid(node_id)) return BLK_NONE;
  NodeView& v = view[node_id];
  char* r = reason[node_id];

  if (!v.seen || !v.online || !v.stalled) {
    r[0] = '\0';
    return BLK_NONE;
  }

  if (!rain_at(node_id)) {
    if (!dryness_corroborated(node_id)) {
      // No comma in this string: sd_log writes it unquoted into a CSV column.
      snprintf(r, sizeof reason[0], "stalled and dry but nothing corroborates it");
      return BLK_SUSPECTED;
    }
    snprintf(r, sizeof reason[0], "stalled and this site is not raining");
    return BLK_CONFIRMED;
  }

  int reporting, stalledCount;
  count_nodes(reporting, stalledCount);

  // Every reporting node stalled at once, in the rain, is weather. It takes at
  // least two nodes to say that: one node stalled is one node stalled, however
  // hard it is raining.
  if (reporting >= 2 && stalledCount == reporting) {
    snprintf(r, sizeof reason[0], "raining and all %d nodes stalled", reporting);
    return BLK_STORM;
  }

  // Raining, but this drain is stalled while at least one neighbour is not.
  // Rain falls on all of them, so the difference is local. This is the
  // discrimination the whole network exists to make.
  snprintf(r, sizeof reason[0], "raining but only %d of %d stalled",
           stalledCount, reporting);
  return BLK_SUSPECTED;
}

const char* blockage_reason(uint8_t node_id) {
  if (!valid(node_id)) return "";
  return reason[node_id];
}

// Lower case throughout, so the gateway line reads as a sentence rather than
// shouting one of the four words at you.
//
//   node 1  blockage = none
//   node 3  blockage = suspected  (raining but only 1 of 3 stalled)
//   node 3  blockage = confirmed  (stalled and this site is not raining)
//   node 2  blockage = storm      (raining and all 3 nodes stalled)
const char* blockage_verdict_name(BlockageVerdict v) {
  switch (v) {
    case BLK_NONE:      return "none";
    case BLK_STORM:     return "storm";
    case BLK_SUSPECTED: return "suspected";
    default:            return "confirmed";
  }
}

#ifdef BLOCKAGE_HOST_TEST
void blockage_test_reset() {
  for (uint8_t i = 0; i <= NUM_NODES; i++) { view[i] = NodeView(); reason[i][0] = '\0'; }
  g_now = 0; g_fc_ok = false; g_fc_mm_x10 = 0;
}
#endif
