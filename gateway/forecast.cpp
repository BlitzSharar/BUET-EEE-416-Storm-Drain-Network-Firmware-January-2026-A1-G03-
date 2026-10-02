#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "forecast.h"

static bool rainNow = false;
static uint16_t mmX10 = 0;
static bool everOk = false;
static uint32_t lastOkMs = 0;      // when the last SUCCESSFUL fetch landed
static uint32_t failStreak = 0;    // consecutive failures, for the backoff

void forecast_update() {
  // Do not attempt a fetch with no association. begin() plus GET() on a down
  // interface is the slowest way to discover something we can already know.
  // A down link does not count as a failed fetch: the backoff exists for the
  // associated-but-no-WAN case, where GET() blocks for its full timeout.
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(FORECAST_URL);
  // Bound the whole transaction. The watchdog is fed at the end of loop(),
  // after this call, so an unbounded fetch resets the gateway. The worst case
  // must stay well inside WDT_TIMEOUT_S.
  http.setConnectTimeout(FORECAST_HTTP_TIMEOUT_MS);
  http.setTimeout(FORECAST_HTTP_TIMEOUT_MS);
  int code = http.GET();
  if (code == 200) {
    // Filter the response before parsing. The Open-Meteo hourly payload carries
    // a full day of values and parsing all of it into a growable document can
    // exhaust the heap on a gateway that is also running WiFi and an SD card.
    // The filter keeps only the field actually used, so memory is bounded no
    // matter how large the response becomes.
    JsonDocument filter;
    filter["current"]["precipitation"] = true;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filter));
    if (err == DeserializationError::Ok) {
      // current.precipitation, for the hour just gone, in the site timezone.
      // See FORECAST_URL in config.h for why this is not hourly[0].
      float mm = doc["current"]["precipitation"] | 0.0f;
      forecast_convert(mm, &mmX10, &rainNow);   // tested in forecast_test.cpp
      everOk = true;
      lastOkMs = (uint32_t)millis();
      failStreak = 0;
    } else {
      failStreak++;
      Serial.printf("forecast parse failed: %s\n", err.c_str());
      // Leave the previous value in place. A failed fetch must not silently
      // report "no rain", because that is the direction that causes a false
      // blockage alarm.
    }
  }
  if (code != 200) {
    failStreak++;
    // Used to be discarded in total silence, so on the serial monitor "no
    // internet", "DNS failed", "TLS failed" and "parsed fine, it is just not
    // raining" all looked identical.
    Serial.printf("forecast fetch failed: HTTP %d\n", code);
  }
  http.end();
}

// FRESHNESS, not just "did one ever work".
//
// A failed fetch deliberately leaves the previous value in place, because
// reporting "no rain" on a network error is the direction that raises a false
// blockage alarm. That is right for a fetch or two. It was wrong for ever, and
// for ever is what it used to mean: nothing here carried a timestamp, so a
// single successful fetch was believed until the gateway was power cycled.
//
// This RETIRES the value rather than only declining to report it, and that is
// not tidiness. A bare timestamp compared with unsigned arithmetic is wrap safe
// for ordinary spans and wrong at the 49.7 day millis() rollover: once the true
// age passes 2^32 ms the difference returns to near zero and a seven week old
// forecast reports itself fresh for another two hours. The gateway never
// sleeps, so 49.7 days of uptime is the ordinary case for a deployed unit.
//
// Clearing everOk the first time the window closes means there is nothing left
// to come back around. Every reader goes through here and the gateway loop
// calls forecast_ok() on every pass, so the retirement happens within
// milliseconds of expiry, long before any wrap. Same treatment, same reasoning,
// as the rain memory in gateway/blockage.cpp.
static bool fresh() {
  if (!everOk) return false;
  if (!forecast_within_window((uint32_t)millis(), lastOkMs)) {
    everOk  = false;
    rainNow = false;
    mmX10   = 0;
    Serial.println("forecast has gone stale and no longer counts as evidence");
    return false;
  }
  return true;
}

bool forecast_rain_now() { return fresh() && rainNow; }
uint16_t forecast_mm_x10() { return fresh() ? mmX10 : 0; }
bool forecast_ok() { return fresh(); }
uint32_t forecast_fail_streak() { return failStreak; }
uint32_t forecast_age_ms() {
  if (!fresh()) return FORECAST_AGE_NONE;   // never worked, or already retired
  return (uint32_t)((uint32_t)millis() - lastOkMs);
}
