#ifndef WATCHDOG_H
#define WATCHDOG_H
#include <stdint.h>

// Hardware watchdog for an unattended field node.
//
// Why this exists: a node sits on a pole for a monsoon season with nobody
// watching it. If the firmware hangs, for example in a blocking sensor read or
// a radio driver that never returns, the node goes silent and stays silent
// until somebody climbs up to it. Field reliability practice for unattended
// devices treats a watchdog as mandatory, not optional.
//
// The important detail: this uses a CHECK IN pattern, not a naive kick. The
// most common watchdog failure in production firmware is feeding the timer on
// every pass through the main loop regardless of whether the device is still
// doing its job, which keeps a hung-but-looping device alive forever. Here the
// watchdog is only fed once a full sample cycle has actually completed, so a
// loop that spins without sampling still triggers a reset.
void wdt_begin();          // arm the watchdog
void wdt_work_completed(); // call ONLY after a real sample cycle finishes

// Call while idle and legitimately waiting for the next sample. Kept separate
// from wdt_work_completed() so the two reasons for feeding stay distinguishable:
// one says "I did my job", the other says "I am waiting, by design".
void wdt_idle_alive();
void wdt_disarm();         // before deliberate deep sleep, so sleep is not a fault

// Reset diagnostics. A watchdog reset with no record is just a mystery reboot,
// so the cause is captured at boot and reported in the first packet.
const char* wdt_last_reset_reason();
bool        wdt_was_watchdog_reset();
uint32_t    wdt_reset_count();     // persists across resets, cleared on power cycle
#endif
