#include <Arduino.h>
#include <esp_system.h>
#include "abs_clock.h"

// RTC memory is retained through deep sleep and cleared only by a true power
// cycle or reset, which is exactly the lifetime we want.
//
// Two separate problems are handled here.
//
// 1. Deep sleep restarts the sketch and millis() begins again at zero, so the
//    time spent asleep must be accumulated explicitly before sleeping.
//
// 2. millis() is 32 bit and wraps every 49.7 days. A node only sleeps while
//    the site is Normal and dry, so during a sustained event, for instance a
//    drain that stays blocked because nobody has cleared it, the node stays
//    awake and the wrap is genuinely reachable. Left unhandled the clock jumps
//    backwards, the unsigned subtraction in the recession test underflows to a
//    huge value, and the node raises an immediate false blockage alarm.
// RTC_DATA_ATTR, deliberately, and NOT RTC_NOINIT_ATTR.
//
// .rtc.data survives a deep-sleep wake and is reloaded from the app image by
// the bootloader on every other kind of reset, so a watchdog reset, a panic or
// a brownout puts this clock back to zero. That is the correct behaviour here,
// because every variable in status_engine.cpp is RTC_DATA_ATTR too and resets
// with it. The clock and the engine state have to agree: a preserved clock
// beside a zeroed recession timer would compute now minus zero, which is days,
// and publish a stall the instant the node came back up.
//
// So the two are reset together and a watchdog reset costs at most one
// recession window of detection, which is the safe direction to lose.
// tests_host/clock_reset_test.cpp drives both reset kinds explicitly, because
// the host shim cannot tell the two attributes apart by itself.
RTC_DATA_ATTR static uint64_t baseMs    = 0;   // time before the current wake
RTC_DATA_ATTR static uint64_t wrapMs    = 0;   // accumulated 32 bit wraps
RTC_DATA_ATTR static uint32_t lastRawMs = 0;   // previous raw millis() reading

void clock_begin() {
  esp_reset_reason_t reason = esp_reset_reason();

  if (reason == ESP_RST_POWERON) {
    // Be explicit. RTC_DATA_ATTR initialisers normally handle a true power-on,
    // but clearing here also protects against a partially retained RTC domain.
    baseMs = 0;
    wrapMs = 0;
    lastRawMs = 0;
    return;
  }

  if (reason != ESP_RST_DEEPSLEEP) {
    // Watchdog, panic, brownout and software resets restart millis() without
    // passing through clock_note_sleep(). Treating the new zero as a 32-bit
    // wrap would jump the clock forward by 49.7 days and fire the recession
    // timer the moment the node came back.
    //
    // This used to say it preserved the last sampled time across such a reset,
    // and on this hardware it does not: the bootloader has already reloaded
    // .rtc.data from the image, so baseMs, wrapMs and lastRawMs are all back at
    // their zero initialisers and the line below adds zero to zero. The
    // behaviour was right, the explanation was not, and an explanation that
    // does not match the mechanism is how somebody later "fixes" it by moving
    // these to .rtc.noinit and gets a preserved clock beside a zeroed engine.
    //
    // Fold whatever is there, which is zero after a reset and the correct
    // running total in a host test that keeps its statics. Either way the
    // counters restart cleanly.
    baseMs += wrapMs + (uint64_t)lastRawMs;
    wrapMs = 0;
    lastRawMs = 0;
  }
  // A deep-sleep wake needs no action: clock_note_sleep() already folded the
  // active time and planned sleep into baseMs and cleared the raw counters.
}

uint64_t clock_now_ms() {
  uint32_t raw = (uint32_t)millis();
  // A reading lower than the previous one can only mean the 32 bit counter
  // wrapped. This works because the firmware samples far more often than
  // once per 49.7 days.
  if (raw < lastRawMs) {
    wrapMs += 0x100000000ULL;
  }
  lastRawMs = raw;
  return baseMs + wrapMs + (uint64_t)raw;
}

void clock_note_sleep(uint32_t sleep_ms) {
  // Fold everything elapsed so far, plus the sleep about to be taken, into
  // the base. millis() restarts at zero on wake, so the wrap accounting and
  // the last raw reading reset with it.
  baseMs    = clock_now_ms() + (uint64_t)sleep_ms;
  wrapMs    = 0;
  lastRawMs = 0;
}
