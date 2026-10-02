#include "node_digest.h"
#include "config.h"

// No Arduino, no radio, no clock. Everything here is arithmetic on values that
// already arrived, which is what lets tests_host/digest_test.cpp drive the tie
// cases directly instead of hoping a fuzz run happens to produce one.

struct Group {
  uint8_t n;                                   // packets collected so far
  uint8_t sevVotes[GW_SEV_WATERLOGGED + 1];    // votes per severity
  int32_t levelSum;
  int32_t rateSum;
  uint8_t rainWet;
};
static Group g[NUM_NODES + 1];

static bool valid(uint8_t id) { return id >= 1 && id <= NUM_NODES; }

static void clear(Group& s) {
  s.n = 0; s.levelSum = 0; s.rateSum = 0; s.rainWet = 0;
  for (uint8_t i = 0; i <= GW_SEV_WATERLOGGED; i++) s.sevVotes[i] = 0;
}

bool digest_note(const NodeReport& rep, Digest* out) {
  if (!valid(rep.node_id) || out == nullptr) return false;
  // A severity outside the enum cannot index the vote array. lora_gw_poll()
  // already rejects those frames, so this is unreachable through the real path,
  // and that is exactly why it is here rather than trusted: this function is
  // also reachable from the host suite and from any future caller, and an out
  // of range write here would corrupt the neighbouring group silently.
  if (rep.severity > GW_SEV_WATERLOGGED) return false;

  Group& s = g[rep.node_id];
  s.sevVotes[rep.severity]++;
  s.levelSum += rep.level_mm;
  s.rateSum  += rep.rate;
  if (rep.rain) s.rainWet++;
  s.n++;

  if (s.n < DIGEST_SAMPLES) return false;

  // THE VOTE. Scan upward and take strictly greater, so on a tie the LAST
  // index examined wins, which is the most severe of the tied values.
  //
  // Ties are not a corner case here: five samples across three categories tie
  // constantly, and 2 ELEVATED against 2 WATERLOGGED with 1 NORMAL is an
  // ordinary group for a drain sitting near LEVEL_SAFE_MM. Breaking toward the
  // more severe means a site oscillating across the road-unsafe line reports
  // the unsafe reading, which is the only direction it is acceptable to be
  // wrong in. Scanning downward with >= would silently invert that.
  uint8_t winner = GW_SEV_NORMAL;
  for (uint8_t i = 0; i <= GW_SEV_WATERLOGGED; i++) {
    if (s.sevVotes[i] > 0 && s.sevVotes[i] >= s.sevVotes[winner]) winner = i;
  }

  out->node_id  = rep.node_id;
  out->severity = winner;
  // Round to nearest rather than truncating. The level is compared against
  // thresholds by eye on the monitor during calibration, and a reading that
  // always sits a millimetre low is a reading that disagrees with the node's
  // own line for no reason anybody can see.
  int32_t ls = s.levelSum;
  out->level_mm = (int16_t)((ls >= 0) ? ((ls + DIGEST_SAMPLES / 2) / DIGEST_SAMPLES)
                                      : ((ls - DIGEST_SAMPLES / 2) / DIGEST_SAMPLES));
  out->rate     = (int8_t)(s.rateSum / DIGEST_SAMPLES);
  // Strictly more than half, so a 2 of 5 wet group reads dry. Rain is only
  // shown here for context; every rain DECISION goes through blockage.cpp,
  // which sees each packet's own plate byte and never this summary.
  out->rain     = (s.rainWet * 2 > DIGEST_SAMPLES);
  out->samples  = s.n;

  clear(s);
  return true;
}

const char* digest_words(uint8_t severity) {
  if (severity >= GW_SEV_WATERLOGGED) return "complete blockage";
  if (severity == GW_SEV_ELEVATED)    return "partial blockage";
  return "";
}

#ifdef DIGEST_HOST_TEST
void digest_test_reset() { for (uint8_t i = 0; i <= NUM_NODES; i++) clear(g[i]); }
#endif
