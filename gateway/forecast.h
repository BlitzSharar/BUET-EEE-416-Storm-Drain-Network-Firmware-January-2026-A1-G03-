#ifndef FORECAST_H
#define FORECAST_H
#include <stdint.h>
#include "config.h"

void forecast_update();          // fetch Open-Meteo over WiFi
bool forecast_rain_now();        // precipitation this hour, by FORECAST_WET_X10
uint16_t forecast_mm_x10();      // that hour's precipitation, mm x 10
bool forecast_ok();              // is there a FRESH forecast? gates the retry rate

// Consecutive failed fetches, for the retry backoff in gateway.ino. WiFi being
// down does not count: no request was made.
uint32_t forecast_fail_streak();

#define FORECAST_AGE_NONE 0xFFFFFFFFUL

// How long since the last successful fetch, or FORECAST_AGE_NONE if there has
// never been one or the last one has been retired as stale. For the serial line and the local UI, so "the forecast says dry"
// and "the forecast has not worked since Tuesday" stop looking identical.
uint32_t forecast_age_ms();

// The millimetres-to-flags conversion, on its own so a host test can drive it
// without WiFi, HTTP or a JSON parser. forecast_update() calls exactly this, so
// what the test exercises is what runs.
//
// Two things were wrong here and both are now fixed by construction. The cast
// used to truncate, which put a live gap between "raining" as this file defined
// it and "raining" as blockage.cpp defined it, right across the drizzle band.
// And a negative or malformed value cast straight to uint16_t wrapped to tens
// of thousands, which reads as a deluge and makes every stall look like a
// storm. That is the direction that disables blockage detection in silence.
// IS A FORECAST TAKEN AT lastOk STILL EVIDENCE AT now?
//
// Header-only for the same reason forecast_convert() is: the decision that
// gates a blockage verdict should be drivable by a test that needs no WiFi, no
// HTTP and no clock. Nothing in forecast.cpp was executed by the suite at all
// before this, which is how the rollover below survived.
//
// Both arguments are raw millis() values and the subtraction is unsigned, so it
// is correct right up to the point where the true age passes 2^32 ms. At that
// point the difference wraps back through zero and a 49.7 day old forecast
// reports itself fresh again for another FORECAST_STALE_MS. The gateway never
// sleeps, so 49.7 days of uptime is the ordinary case for a deployed unit, and
// both directions are the failures this window exists to prevent: a stale WET
// value makes every stall look like rain, and a stale DRY value lets one lone
// plate confirm a blockage.
//
// Unsigned arithmetic cannot distinguish the two, so this function cannot fix
// it on its own. forecast.cpp retires the value the first time this returns
// false, exactly as gateway/blockage.cpp retires an expired rain memory, so by
// the time the counter wraps there is nothing left to come back around.
inline bool forecast_within_window(uint32_t now_ms, uint32_t last_ok_ms) {
  return (uint32_t)(now_ms - last_ok_ms) <= (uint32_t)FORECAST_STALE_MS;
}

inline void forecast_convert(float mm, uint16_t* mm_x10_out, bool* rain_out) {
  if (!(mm > 0.0f)) mm = 0.0f;             // also catches NaN
  if (mm > 6000.0f) mm = 6000.0f;          // 6000 mm/h, far past any real reading
  uint16_t x10 = (uint16_t)(mm * 10.0f + 0.5f);
  *mm_x10_out = x10;
  *rain_out   = (x10 >= FORECAST_WET_X10); // ONE definition, shared with blockage.cpp
}

#endif
