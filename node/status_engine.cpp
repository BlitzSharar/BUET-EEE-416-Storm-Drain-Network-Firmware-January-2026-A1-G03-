#include <Arduino.h>
#include "abs_clock.h"
#include "config.h"
#include "status_engine.h"

// Rule engine: two concurrent machines, a recession timer, and a debounce on
// each machine's output.
//
// Everything here is RTC_DATA_ATTR because the node deep sleeps between samples
// while the site is dry. Deep sleep restarts the sketch, so ordinary statics
// would reset on every wake and the recession timer, which is the one piece of
// state that must accumulate across a long quiet period, would never get
// anywhere. RTC memory survives sleep and is cleared only by a true power cycle.

// --- severity machine ---
RTC_DATA_ATTR static Severity curSev  = SEV_NORMAL;
RTC_DATA_ATTR static Severity candSev = SEV_NORMAL;
RTC_DATA_ATTR static int      sevAgree = 0;

// --- trend machine ---
// curMotion is the WINDOW VERDICT and only ever holds STEADY, RISING or
// RECEDING. curTrend is what the rest of the system sees, and it is recomputed
// from curMotion plus the recession timer on every single sample.
//
// Keeping them apart is not tidiness. The first version stored STALLED straight
// into the one trend variable, so once the recession timer had fired the value
// persisted until the next trend window closed, which meant blockage could
// still be published after the rain came back. fuzz_test caught it. The
// differential oracle did not, because the reference implementation had been
// written with the same mistake in it: two implementations agreeing proves
// they agree, not that they are right.
RTC_DATA_ATTR static Trend    curMotion       = TR_STEADY;
RTC_DATA_ATTR static Trend    curTrend        = TR_STEADY;
RTC_DATA_ATTR static bool     trendAnchored   = false;
RTC_DATA_ATTR static uint64_t trendAnchorMs   = 0;
RTC_DATA_ATTR static int      trendAnchorLevel = 0;

// --- recession timer, which is what makes a blockage inferable at all ---
RTC_DATA_ATTR static bool     recedeRunning    = false;
RTC_DATA_ATTR static uint64_t recedeStartMs    = 0;
RTC_DATA_ATTR static int      recedeStartLevel = 0;
RTC_DATA_ATTR static int      recedeMinLevel   = 0;

// --- stall output, debounced like a published state because it drives cadence ---
RTC_DATA_ATTR static bool     stallOut   = false;
RTC_DATA_ATTR static bool     stallCand  = false;
RTC_DATA_ATTR static int      stallAgree = 0;

// --- safety interlock, read from the raw level, never from a published state ---
RTC_DATA_ATTR static bool     roadUnsafe = false;

static Severity severity_from_level(int level) {
  if (level >= LEVEL_SAFE_MM) return SEV_WATERLOGGED;
  if (level >= LEVEL_HIGH_MM) return SEV_ELEVATED;
  return SEV_NORMAL;
}

EngineOut engine_update(int level, int rate) {
  uint64_t now_ms = clock_now_ms();

  // Safety interlock first, from the raw reading. See the header for why this
  // cannot wait for the debounce.
  roadUnsafe = (level >= LEVEL_SAFE_MM);

  // ---------------- severity, debounced ----------------
  Severity rawSev = severity_from_level(level);
  if (rawSev == candSev) { if (sevAgree < DEBOUNCE_SAMPLES) sevAgree++; }
  else { candSev = rawSev; sevAgree = 1; }
  if (sevAgree >= DEBOUNCE_SAMPLES) curSev = candSev;

  // ---------------- trend, over a fixed window of TIME ----------------
  // Anchored on the absolute clock, not millis(), for the same reason the
  // recession timer is: millis() restarts at zero on every wake, so a span
  // computed from it across a sleep is arithmetic on two unrelated timebases.
  //
  // The window is a duration rather than a sample count because the sampling
  // interval changes with state. A deadband applied to N samples would mean
  // 60 mm/min at the bring-up cadence and 2 mm/min at the dry cadence, so the
  // field would be most sensitive exactly when nothing is happening.
  //
  // TREND_DEADBAND_MM is set from measurement, not taste. Across ten still
  // water stretches in the 18 September capture the worst span seen over a
  // window was 5 mm, with 4 mm at the 95th percentile. See CALIBRATION.
  Trend motion = curMotion;
  if (!trendAnchored) {
    trendAnchored = true;
    trendAnchorMs = now_ms;
    trendAnchorLevel = level;
    motion = TR_STEADY;
  } else if (now_ms >= trendAnchorMs &&
             (now_ms - trendAnchorMs) >= (uint64_t)TREND_WINDOW_MS) {
    uint64_t elapsed = now_ms - trendAnchorMs;

    // SCALE THE DEADBAND TO THE TIME ACTUALLY ELAPSED.
    //
    // The window closes at the first sample at or after TREND_WINDOW_MS, so
    // when the sampling interval is longer than the window the comparison
    // really spans the sampling interval. A fixed 8 mm across 180 s is
    // 2.67 mm/min against 8 mm/min everywhere else, and power.cpp reaches
    // 180 s by tripling the dry cadence on a low battery. The field was three
    // times more sensitive in the quietest state, which is the same inversion
    // the duration window was introduced to remove.
    if (elapsed > (uint64_t)TREND_WINDOW_MS * TREND_STALE_FACTOR) {
      // Too far apart to describe a trend. Re-anchor and say nothing rather
      // than scaling the band up to a number no real rise could ever clear.
      // Safety does not depend on this: engine_road_unsafe() reads the level.
      motion = TR_STEADY;
    } else {
      // Round the band UP, not down. For integers, span >= ceil(D * e / W) is
      // exactly span * W >= D * e, so the threshold is the true rate with no
      // rounding slack in either direction. Truncating instead would let a
      // level that is fractionally below the rate register as a trend, which is
      // a small thing on its own and a divergence from the specification that
      // tests_host/diff_test.cpp checks against.
      uint64_t need = (uint64_t)TREND_DEADBAND_MM * elapsed;
      int band = (int)((need + (uint64_t)TREND_WINDOW_MS - 1) /
                       (uint64_t)TREND_WINDOW_MS);
      // No floor needed. This branch only runs once elapsed >= TREND_WINDOW_MS,
      // so ceil(D * elapsed / W) >= D always, and a floor here was unreachable.
      // Setting it to an absurd value changed no test, which is how it was
      // found. An unreachable clamp reads as a guarantee that something is
      // enforced, and there is nothing here to enforce.
      int span = level - trendAnchorLevel;
      if (span >= band)       motion = TR_RISING;
      else if (span <= -band) motion = TR_RECEDING;
      else                    motion = TR_STEADY;
    }
    trendAnchorMs = now_ms;
    trendAnchorLevel = level;
  }

  // ---------------- recession timer ----------------
  // The drain is holding water it should have shed. LEVEL ONLY: there is
  // deliberately no rain term here any more.
  //
  // A node cannot tell a blocked drain from one being out-rained, because both
  // look like water that is not going down. Rainfall is half of what separates
  // them and the shape of the event across the network is the other half, which
  // no single node can see. So the node measures and the gateway judges.
  //
  // Every node carries a plate now, so a node COULD gate this on its own
  // rainfall. It deliberately does not. Gating here would suppress the stall
  // during rain, which is exactly when a blocked drain matters, and the gateway
  // would never get to compare one stalled drain against two that are draining
  // in the same downpour.
  //
  // It still re-anchors the moment a real drop is proven, so a later rise can
  // never erase a good drop and manufacture a stall retroactively.
  bool stalled = false;
  if (level >= LEVEL_HIGH_MM) {
    if (!recedeRunning) {
      recedeRunning = true;
      recedeStartMs = now_ms;
      recedeStartLevel = level;
      recedeMinLevel = level;
    } else {
      if (level < recedeMinLevel) recedeMinLevel = level;
      if ((recedeStartLevel - recedeMinLevel) >= RECEDE_DROP_MM) {
        recedeStartMs = now_ms;
        recedeStartLevel = level;
        recedeMinLevel = level;
      } else if ((now_ms - recedeStartMs) >= (uint64_t)RECEDE_WINDOW_MS) {
        stalled = true;
      }
    }
  } else {
    recedeRunning = false;
  }

  // STALLED is a refinement of STEADY, so it overrides the window verdict, with
  // one exception. If the short window has just seen the level fall, believe
  // that recent evidence over the twenty minute judgement: the re-anchor is
  // about to agree with it anyway, and suppressing a blockage on a drain that
  // is visibly draining is the safe direction to be wrong in.
  curMotion = motion;                       // never STALLED, so it cannot go stale

  // ---------------- stall: derived, debounced, THEN published ----------------
  // The order here is load-bearing and it used to be wrong.
  //
  // curTrend was set to TR_STALLED before the debounce ran, and curTrend is
  // what engine_pack() puts on the air. The gateway recomputes the stall from
  // that byte, so it saw the RAW stall: the very first sample to trip the
  // recession rule was transmitted immediately, on the change-triggered uplink,
  // and could be escalated to CONFIRMED there and then. The debounced value
  // never left the node. It gated cadence and sleep and nothing else, so the
  // three-sample filter contributed nothing at all to the published alert, and
  // every test passed because they all read the debounced field the node itself
  // was using.
  //
  // Debounce first, and let the debounced result be the only thing that can
  // name the trend STALLED. Now there is one stall, the wire carries it, the
  // label shows it and engine_stalled() agrees with the gateway by construction.
  // The debounce is deliberately ASYMMETRIC. Raising a stall takes
  // DEBOUNCE_SAMPLES agreeing samples, because it ends in a maintenance callout
  // to a named drain. Clearing one takes effect immediately, because the thing
  // that clears it is direct evidence against it: the level has fallen below
  // LEVEL_HIGH_MM, or the short window can see the water going down. Holding a
  // stall on a drain that is visibly draining is the wrong direction to be slow
  // in, and it is the same reasoning that lets recession override the twenty
  // minute recession timer a few lines above.
  bool rawStall = (curSev >= SEV_ELEVATED) && stalled && (motion != TR_RECEDING);
  if (!rawStall) {
    // stallAgree goes to ZERO, not to DEBOUNCE_SAMPLES. Both behave identically
    // today, because stallCand is cleared on the same line so the next stalled
    // sample always takes the else branch and resets the counter to 1. But a
    // counter left pre-loaded at the publish threshold is a landmine: remove
    // the stallCand assignment in some later edit and the very first stalled
    // sample publishes immediately, which is the defect this whole debounce
    // exists to prevent.
    stallOut = false; stallCand = false; stallAgree = 0;
  } else {
    if (stallCand) { if (stallAgree < DEBOUNCE_SAMPLES) stallAgree++; }
    else { stallCand = true; stallAgree = 1; }
    if (stallAgree >= DEBOUNCE_SAMPLES) stallOut = true;
  }

  curTrend = stallOut ? TR_STALLED : motion;

  EngineOut out = { curSev, curTrend, stallOut };
  return out;
}

EngineOut engine_current() {
  EngineOut out = { curSev, curTrend, stallOut };
  return out;
}

Attention engine_sampling_attention() {
  Severity pending = candSev > curSev ? candSev : curSev;
  EngineOut probe = { pending, curTrend, stallOut };
  return engine_attention(probe);
}

bool engine_road_unsafe() { return roadUnsafe; }

// ONE DEPTH WORD PLUS ONE MOTION WORD, and the motion word means the same
// thing on every row.
//
// The old table used three different words for the same fact: CLEARING at
// shallow depth, DRAINING at knee depth, WLOG DRAIN at road depth, all meaning
// the level is falling. Ten arbitrary names to learn instead of three depths
// and four motions, and nothing in the name told you which was which.
//
//                  steady        rising              falling              not draining
//   under 100 mm   NORMAL        NORMAL RISING       NORMAL CLEARING      cannot happen
//   100 to 149     ELEVATED      ELEVATED RISING     ELEVATED CLEARING    ELEVATED BLOCKED
//   150 and up     WATERLOG      WATERLOG RISING     WATERLOG CLEARING    WATERLOG BLOCKED
//
// The longest is 17 characters against 21 available on the OLED at this font.
//
// BLOCKED is a local read and the node cannot verify it. One drain holding
// water looks identical whether it is blocked or being out-rained, and during
// a storm all three nodes will show BLOCKED while the gateway correctly reports
// blockage = storm. That disagreement is the design working: the node reports
// what it measured, the network decides what it means. See gateway/blockage.h.
const char* engine_label(Severity s, Trend t) {
  switch (s) {
    case SEV_NORMAL:
      // Shallow water climbing fast is not a road hazard, so the beacon stays
      // dark, but it is worth saying. STALLED cannot reach here: the recession
      // timer is only armed at or above LEVEL_HIGH_MM.
      if (t == TR_RISING)   return "NORMAL RISING";
      if (t == TR_RECEDING) return "NORMAL CLEARING";
      return "NORMAL";
    case SEV_ELEVATED:
      if (t == TR_STALLED)  return "ELEVATED BLOCKED";
      if (t == TR_RISING)   return "ELEVATED RISING";
      if (t == TR_RECEDING) return "ELEVATED CLEARING";
      return "ELEVATED";
    default:
      if (t == TR_STALLED)  return "WATERLOG BLOCKED";
      if (t == TR_RISING)   return "WATERLOG RISING";
      if (t == TR_RECEDING) return "WATERLOG CLEARING";
      return "WATERLOG";
  }
}
