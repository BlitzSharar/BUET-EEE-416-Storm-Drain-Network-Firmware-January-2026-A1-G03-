#ifndef POWER_H
#define POWER_H
#include <stdint.h>
#include "status_engine.h"

void power_init();
void power_note_boot(bool cold_boot);
void power_poll();
uint8_t power_battery_pct();
bool power_low();

// Adaptive cadence: chosen by state and rainfall, then stretched if the
// battery is low. This is the core of the design review response.
// ms_stable is how long the current state has been unchanged. A long-stable
// state is backed off, because it carries no new information at the fast rate.
unsigned long power_sample_interval(Attention att, bool raining, uint64_t ms_stable = 0);
unsigned long power_report_interval(Attention att, bool raining, uint64_t ms_stable = 0);

// True when the node may deep sleep until the next sample.
bool power_may_sleep(Attention att, bool stalled, bool raining, uint64_t ms_stable = 0);

// OLED policy: on at boot, on for a while after a state change, on whenever
// the site is not Normal. Off otherwise, which removes a 15 mA load.
bool power_oled_on(Attention att, uint64_t ms_since_change);
#endif
