#ifndef BEACON_H
#define BEACON_H
#include "config.h"
#include "status_engine.h"
void beacon_init();
// Takes SEVERITY, never the trend. This is the safety property of the whole
// design: the road is unsafe at a depth, not at a direction, so water that is
// falling can never switch the warning off.
// THE ALERT PATTERN, as one pure function.
//
// Here in the header rather than inside beacon.cpp because tests_host/orch_test
// kept its own copy of the pattern as a lambda, and a second copy of a rule is
// a rule that can drift. It is a pure function of severity and the clock, so a
// test can drive it directly with no pin and no hardware.
//
//   NORMAL       silent
//   ELEVATED     BUZZ_ELEVATED_ON_MS in every BUZZ_ELEVATED_PERIOD_MS
//   WATERLOGGED  BUZZ_WATERLOG_ON_MS in every BUZZ_WATERLOG_PERIOD_MS
//
// A continuous tone is the case where the on-time equals the period, so there
// is no special branch for it and nothing to keep in step.
inline bool buzzer_should_sound(Severity sev, unsigned long ms) {
  unsigned long on, period;
  if (sev >= SEV_WATERLOGGED)   { on = BUZZ_WATERLOG_ON_MS; period = BUZZ_WATERLOG_PERIOD_MS; }
  else if (sev >= SEV_ELEVATED) { on = BUZZ_ELEVATED_ON_MS; period = BUZZ_ELEVATED_PERIOD_MS; }
  else                          return false;
  return (ms % period) < on;
}

// THE BEACON PATTERN, as one pure function, for the same reason.
//
// This rule used to live inline in beacon.cpp as a bare
// `digitalWrite(PIN_BEACON, (millis() / 500) % 2)`. That made it the only
// output rule in the project with no test behind it, while the buzzer beside it
// had a pure function, a host test and a boot chirp. The beacon is the output a
// member of the public actually sees, so it had the least coverage and the most
// consequence.
//
//   NORMAL       dark
//   ELEVATED     BEACON_BLINK_ON_MS in every BEACON_BLINK_PERIOD_MS
//   WATERLOGGED  solid
//
// Solid is the case where the on-time equals the period, so there is no special
// branch and nothing to keep in step.
//
// Takes SEVERITY only. Trend must never reach this function. Water that is
// falling is still over the road, and a warning that eases as the level drops
// is a warning that goes dark while the road is still impassable.
inline bool beacon_should_light(Severity sev, unsigned long ms) {
  if (sev >= SEV_WATERLOGGED) return true;
  if (sev >= SEV_ELEVATED)    return (ms % BEACON_BLINK_PERIOD_MS) < BEACON_BLINK_ON_MS;
  return false;
}

void beacon_set(Severity sev);   // silent / beeping / continuous

// Re-apply the last commanded severity. Both patterns are computed from
// millis() at the instant of the call and the pin then holds that value, so a
// stretch of code that blocks for seconds freezes the beacon and the buzzer
// mid-pattern. lora_tx_jitter() delays up to TX_JITTER_MS and lora_rx_window()
// busy-waits for RX_WINDOW_MS, which together is over four seconds on the
// transmit path, immediately after beacon_set() was last called.
//
// Those loops call this. It is two digitalWrite calls and costs nothing.
void beacon_tick();
#endif
