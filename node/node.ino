// EEE 416 Storm-Drain Network: field node firmware
// Group 03, Section A1.
//
// Design review response, implemented here:
//  - sampling and reporting cadence adapt to state and rainfall
//  - the node deep sleeps between samples while the site is dry
//  - the OLED is powered down unless something is happening
//  - transmissions carry randomised jitter to break repeating collisions
//  - a node built WITHOUT its own rain plate opens a receive window after each
//    transmission, so even a sleeping one can be told about rainfall. Every
//    node ships with a plate now, so that window is off by default
#include <Arduino.h>
#include "config.h"
#include "sensor_level.h"
#include "sensor_rain.h"
#include "status_engine.h"
#include "beacon.h"
#include "lora_tx.h"
#include "lora_rx.h"
#include "ui_local.h"
#include "power.h"
#include "abs_clock.h"
#include "watchdog.h"

// Step tracing through setup(). On by default: it costs a few bytes and a few
// milliseconds, and without it a board that hangs during init produces no
// output at all, which is the hardest possible thing to diagnose. The first
// bench bring-up of this project hung somewhere in setup and the log showed
// nothing but watchdog resets.
#define TRACE_SETUP 1
#if TRACE_SETUP
  #define STEP(msg) do { Serial.print("  ... "); Serial.println(msg); Serial.flush(); } while (0)
#else
  #define STEP(msg) do {} while (0)
#endif
#include <esp_sleep.h>

// State that must survive deep sleep lives in RTC memory. Deep sleep restarts
// the sketch, so anything else is reset on every wake.
RTC_DATA_ATTR static uint32_t bootCount = 0;
RTC_DATA_ATTR static uint64_t lastReportMs = 0;       // absolute clock
// Published and observed state are now a (severity, trend) pair. They are
// compared as the packed byte, which is exactly what goes on the air, so a
// change that the gateway would not see never counts as a change here either.
RTC_DATA_ATTR static uint8_t  lastPublished = 0;   // engine_pack of SEV_NORMAL/TR_STEADY
RTC_DATA_ATTR static uint8_t  lastObserved  = 0;
RTC_DATA_ATTR static uint64_t lastChangeMs = 0;
RTC_DATA_ATTR static uint64_t lastTxAttemptMs = 0;
RTC_DATA_ATTR static bool     lastTxFailed = false;

unsigned long tSample = 0;   // within one wake cycle only

static uint64_t stable_elapsed(uint64_t now, uint64_t then) {
  return now >= then ? now - then : 0;
}

static uint64_t report_elapsed(uint64_t now, uint64_t then) {
  // A retained timestamp ahead of the recovered absolute clock means the RTC
  // state was inconsistent (for example after reflashing). Force a heartbeat
  // instead of waiting for a 64-bit underflow to wrap around again.
  return now >= then ? now - then : UINT64_MAX;
}

static void do_sample_and_maybe_report(bool forceReport) {
  int level   = level_read_mm();
  int rate    = level_rate_mm_min();
  RainInput rain = rain_get();
  uint8_t batt = power_battery_pct();

  // Nothing has ever come back from the sensor: unplugged, miswired, or
  // submerged since power on. There is no level to reason about, and feeding
  // the engine a stand-in is worse than staying quiet, because the engine
  // cannot tell an invented reading from a measured one. Held at any value at
  // or above LEVEL_HIGH_MM with no rain it publishes BLOCKAGE about twenty
  // minutes later, which is a maintenance callout to a named drain generated
  // from no data at all.
  //
  // Staying silent is the honest signal. The gateway calls the node offline
  // after NODE_TIMEOUT_MS and raises node_offline, which is true: this node is
  // not reporting. The watchdog is still fed, because the firmware is working
  // correctly; it is the hardware that is not. The node retries every cycle
  // and starts reporting the moment a real echo arrives.
  if (!level_last_sample_valid()) {
    Serial.println("no reliable ultrasonic sample: check wiring, 5V rail, "
                   "mounting/dead zone and the 20 us trigger; report withheld");
    EngineOut held = engine_current();
    uint64_t now_ms = clock_now_ms();
    ui_set_power(power_oled_on(engine_attention(held),
                               stable_elapsed(now_ms, lastChangeMs)));
    ui_show(level_have_reading() ? level_last_mm() : 0, 0, held, batt);
    wdt_work_completed();
    return;
  }

  // The engine takes no rain input at all any more. A node can measure that
  // water is not going down; it cannot measure why. The gateway turns that
  // measurement into a blockage verdict, because it has every node's rain plate,
  // the forecast and the shape of the event across all three sites, and the
  // last of those is the one no node can ever see. See gateway/blockage.h.
  //
  // This node's own plate is still read, still transmitted and still drives the
  // sampling cadence. It simply no longer gates the recession rule. Gating it
  // here would suppress the stall during rain, which is exactly when a blocked
  // drain matters, and the gateway would never get to compare one stalled drain
  // against two that are draining in the same downpour.
  EngineOut st = engine_update(level, rate);
  uint8_t packed = engine_pack(st);

  uint64_t now_ms = clock_now_ms();
  if (packed != lastObserved) {
    lastChangeMs = now_ms;
    lastObserved = packed;
  }

  // Severity only. The trend never reaches the beacon, by design.
  beacon_set(st.severity);
  ui_set_power(power_oled_on(engine_attention(st),
                             stable_elapsed(now_ms, lastChangeMs)));
  ui_show(level, rate, st, batt);

  // Compute the send decision BEFORE printing it. The print used to test
  // (forceReport || packed != lastPublished) while the send tested that AND the
  // retry back-off, so after a failed transmission the serial line claimed a
  // transmission that never happened. Exactly the shape of the rain flag bug:
  // a line reporting a different expression from the one that acts.
  bool stateNeedsPublication = (packed != lastPublished);
  bool retryAllowed = !lastTxFailed ||
      report_elapsed(now_ms, lastTxAttemptMs) >= (uint64_t)TX_FAILURE_RETRY_MS;
  bool willSend = (forceReport || stateNeedsPublication) && retryAllowed;

#if BRINGUP_MODE
  {
    // Words rather than numbers. sev=2 trend=3 made you hold a key in your head
    // to read your own log, and the combined state word already carries both,
    // so printing all three was redundant as well as cryptic.
    Serial.printf("sample: level=%dmm rate=%d rain=%s  %s%s\n",
                  level, rate, rain.raining ? "wet" : "dry",
                  engine_label(st),
                  willSend ? "   -> TRANSMITTING" : "");
  }
#endif

  // The sample cycle completed, so the node is genuinely doing its job. This
  // is the only place the watchdog is fed.
  wdt_work_completed();

  if ((forceReport || stateNeedsPublication) && retryAllowed) {
    lastTxAttemptMs = now_ms;
    lora_tx_jitter();
    if (lora_send_report(level, rate, rain, st, batt)) {
#if NODE_EXPECTS_GW_REPLY
      // Only a plate-less node has anything to wait for here. This used to run
      // unconditionally, holding the receiver on for 1500 ms after every report
      // for a reply the gateway no longer sends and this node would not read.
      lora_rx_window(RX_WINDOW_MS);      // catch the gateway rainfall reply
#endif
      lastReportMs = clock_now_ms();
      lastPublished = packed;
      lastTxFailed = false;
    } else {
      lastTxFailed = true;
      Serial.println("report not published; state remains pending for retry");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);                          // let the host attach before we talk
  Serial.println();
  Serial.println("=== node boot ===");
  Serial.flush();
  clock_begin();
  bootCount++;
  wdt_begin();                        // arm before anything can hang
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool timerWake = (cause == ESP_SLEEP_WAKEUP_TIMER);
#if BRINGUP_MODE
  Serial.println();
  Serial.println("=====================================================");
  Serial.printf ("  BRING-UP MODE, node %d.  Sampling every 2 s,\n", NODE_ID);
  Serial.println("  reporting every 10 s, deep sleep disabled.");
  Serial.println("  Set BRINGUP_MODE to 0 in config.h for real use.");
  Serial.println("=====================================================");
#endif
  if (wdt_was_watchdog_reset()) {
    Serial.printf("RECOVERED from %s, reset count %lu\n",
                  wdt_last_reset_reason(), (unsigned long)wdt_reset_count());
  }
  STEP("power");
  power_init();
  power_note_boot(!timerWake);
  STEP("display");
  ui_init();
  if (timerWake) ui_set_power(false);  // do not flash the OLED on every dry wake
  STEP("ultrasonic");
  level_init();
  STEP("rain sensor");
  rain_init();
  STEP("beacon and buzzer");
  beacon_init();
  STEP("LoRa radio");
  lora_init();
  lora_rx_listen();

  if (!timerWake) {
    STEP("splash");
    ui_splash();                        // cold boot: show the installer a splash
    lastChangeMs = clock_now_ms();
  }

  // After a timer wake the node has one job: sample, decide, report if due.
  //
  // "If due" is the point. This used to force a report on every wake, which
  // meant REPORT_DRY_MS was never consulted on the sleeping path, and the
  // sleeping path is every dry cycle. The documented 15 minute dry heartbeat
  // was therefore actually a 60 second one: fifteen times the intended airtime,
  // and it contradicted the cadence table the power budget is built on. Any
  // state change still transmits immediately, because do_sample_and_maybe_report
  // reports on escalation regardless of what is passed here.
  tSample = millis();
  if (timerWake) {
    Attention att_prev = engine_sampling_attention();
    RainInput rain_prev = rain_get();
    uint64_t now_ms = clock_now_ms();
    uint64_t since_report_ms = report_elapsed(now_ms, lastReportMs);
    uint64_t stable_ms = stable_elapsed(now_ms, lastChangeMs);
    bool due = since_report_ms >=
               power_report_interval(att_prev, rain_prev.raining, stable_ms);
    do_sample_and_maybe_report(due);
  } else {
    // COLD BOOT: sample and report immediately, unconditionally.
    //
    // Without this a freshly flashed or freshly installed node takes its first
    // sample after SAMPLE_DRY_MS and then does not transmit until REPORT_DRY_MS,
    // so it is silent for fifteen minutes. An installer standing at a site has
    // no way to tell whether the node works, and on the bench it looks broken.
    //
    // A boot is also genuinely worth reporting: it tells the gateway the node
    // exists, gives the offline detector a baseline, and after the watchdog
    // recovery above it says the node came back.
    STEP("first sample and report");
    do_sample_and_maybe_report(true);
  }
  STEP("setup complete, entering loop");
}

void loop() {
  unsigned long now = millis();
  Attention att = engine_sampling_attention();
  RainInput rain = rain_get();

  uint64_t stable_ms = stable_elapsed(clock_now_ms(), lastChangeMs);
  unsigned long sampleInterval = power_sample_interval(att, rain.raining, stable_ms);
  if (now - tSample >= sampleInterval) {
    tSample = now;
    uint64_t since_report_ms = report_elapsed(clock_now_ms(), lastReportMs);
    bool due = since_report_ms >= power_report_interval(att, rain.raining, stable_ms);
    do_sample_and_maybe_report(due);

    // The sample may have changed the debounce candidate, published state or
    // rain input. Recompute the idle cadence before deciding to feed/sleep.
    att = engine_sampling_attention();
    rain = rain_get();
    stable_ms = stable_elapsed(clock_now_ms(), lastChangeMs);
    sampleInterval = power_sample_interval(att, rain.raining, stable_ms);
  }

  // Feed from the loop as well, but only while the node is genuinely idle and
  // waiting for the next sample. Without this the watchdog fires during the
  // long dry interval, when nothing is wrong. The guard matters: if a sample
  // cycle itself hangs, tSample stops advancing, this branch stops being taken
  // and the watchdog still does its job.
  if ((unsigned long)(millis() - tSample) < sampleInterval) {
    wdt_idle_alive();
  }

  lora_rx_poll();
  power_poll();

  // Drive the local warning on every pass, not once per sample cycle.
  //
  // beacon_set() generates two time-based patterns from millis(): the ELEVATED
  // caution blink, which toggles every 500 ms and so runs at 1 Hz, and the
  // buzzer chirp of 150 ms twice inside every 10 s. Both key on SEVERITY, which
  // is the depth, not on the trend. Both are computed at the instant of the
  // call and the pin then holds that value until the next call, so calling it
  // only from the sample cycle aliases both patterns against the sample rate.
  //
  // SAMPLE_EVENT_MS is 5000 and the chirp pattern repeats every 10000, which
  // is exactly synchronous: the call lands on the same two phases forever. In
  // measurement, the buzzer held HIGH for 300 s out of every 600 rather than
  // the intended 18, a 50 percent duty cycle, which is the continuous tone the
  // chirp pattern exists to avoid and roughly sixteen times the energy. The
  // blink changed state once in ten minutes, so the LED simply looked stuck.
  //
  // This is cheap: two digitalWrite calls on a loop that has no delay in it.
  beacon_set(engine_current().severity);

  // While the site is dry there is nothing to watch, so sleep the controller
  // until the next sample is due. This is what makes the solar budget close.
  att = engine_sampling_attention();
  rain = rain_get();
  stable_ms = stable_elapsed(clock_now_ms(), lastChangeMs);
  if (power_may_sleep(att, engine_current().stalled, rain.raining, stable_ms)) {
    unsigned long interval = power_sample_interval(att, rain.raining, stable_ms);
    unsigned long elapsed  = millis() - tSample;
    if (interval > elapsed + DEEP_SLEEP_MIN_MS) {
      unsigned long sleep_ms = interval - elapsed;
      ui_set_power(false);
      Serial.printf("sleeping %lu ms (boot %lu)\n", sleep_ms, (unsigned long)bootCount);
      Serial.flush();
      wdt_disarm();                           // planned sleep is not a fault
      clock_note_sleep((uint32_t)sleep_ms);   // must precede the sleep call
      esp_sleep_enable_timer_wakeup((uint64_t)sleep_ms * 1000ULL);
      esp_deep_sleep_start();           // does not return; setup() runs on wake
    }
  }
}
