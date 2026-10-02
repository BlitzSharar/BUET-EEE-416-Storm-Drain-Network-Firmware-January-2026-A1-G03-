#include <Arduino.h>
#include "abs_clock.h"
#include "config.h"
#include "power.h"

// Ratio of the sense divider. 100k over 100k gives 2.0. Verify against a
// multimeter during bring-up: resistor tolerance alone moves this a few
// percent, and this is the number that decides when the node declares itself
// low on battery.
static const float DIVIDER = 2.0f;

// RTC backed. Deep sleep restarts the sketch and clears ordinary statics, and
// power_poll() only runs from loop(), which a sleeping node barely reaches. As
// an ordinary static this flag was therefore false on every wake, so the low
// battery interval stretching never took effect in the dry state, which is the
// state it exists for. Carrying the last evaluation across the sleep fixes it,
// and power_init() re-evaluates it on every wake anyway.
RTC_DATA_ATTR static bool lowPower = false;
RTC_DATA_ATTR static uint8_t cachedBatteryPct = 100;
RTC_DATA_ATTR static uint64_t oledBootUntilMs = 0;
static unsigned long lastBatteryPollMs = 0;
static bool haveBatterySampleThisWake = false;

static float vbat();
static uint8_t read_battery_pct();

void power_init() {
  pinMode(PIN_VBAT, INPUT);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);
  // Evaluate immediately. setup() runs on every wake from deep sleep and then
  // samples and reports without ever reaching loop(), so without this the
  // cadence decisions on the sleeping path would use a stale or unset flag.
  cachedBatteryPct = read_battery_pct();
  lowPower = (cachedBatteryPct < BATT_LOW_PCT);
  lastBatteryPollMs = millis();
  haveBatterySampleThisWake = true;
}

void power_note_boot(bool cold_boot) {
  if (cold_boot) {
    oledBootUntilMs = clock_now_ms() + (uint64_t)OLED_ON_AT_BOOT_MS;
  }
}

static float vbat() {
  // analogReadMilliVolts applies the per-chip calibration burned into eFuse.
  // The naive raw/4095*3.3 conversion is wrong on this part: the ESP32 ADC is
  // non-linear, ignores roughly the first 0.21 V at the default 11 dB
  // attenuation, saturates near 3.1 V, and its internal reference varies from
  // about 1.0 to 1.2 V between individual chips. Averaging several reads also
  // suppresses the well known sample-to-sample noise.
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT);
  mv /= 8;
  return ((float)mv / 1000.0f) * DIVIDER;
}

static uint8_t read_battery_pct() {
  float v = vbat();
  float pct = (v - 3.2f) / (4.2f - 3.2f) * 100.0f;
  return (uint8_t)constrain(pct, 0.0f, 100.0f);
}

uint8_t power_battery_pct() { return cachedBatteryPct; }

void power_poll() {
  unsigned long now = millis();
  if (haveBatterySampleThisWake &&
      (unsigned long)(now - lastBatteryPollMs) < BATT_POLL_MS) return;
  cachedBatteryPct = read_battery_pct();
  lowPower = (cachedBatteryPct < BATT_LOW_PCT);
  lastBatteryPollMs = now;
  haveBatterySampleThisWake = true;
}
bool power_low() { return lowPower; }

unsigned long power_sample_interval(Attention att, bool raining, uint64_t ms_stable) {
  // A state that has been stable for a long time is backed off. Waterlogging
  // is excluded: the beacon has to stay driven and the road is still unsafe.
  if (ms_stable >= SUSTAINED_AFTER_MS && att != ATT_QUIET && !engine_road_unsafe()) {
    return lowPower ? SUSTAINED_SAMPLE_MS * SUSTAINED_LOW_BATTERY_SLOWDOWN
                    : SUSTAINED_SAMPLE_MS;
  }
  // SAMPLE_EVENT_MS and SAMPLE_RISING_MS currently hold the same value. They
  // are kept as separate constants deliberately so the two cases can be tuned
  // independently after calibration; this is not a duplicated branch by error.
  unsigned long base;
  if (att >= ATT_EVENT)           base = SAMPLE_EVENT_MS;
  // NOLINTNEXTLINE(bugprone-branch-clone) equal by choice, tunable separately
  else if (att == ATT_WATCH)      base = SAMPLE_RISING_MS;
  else if (raining)               base = SAMPLE_WET_MS;
  else                            base = SAMPLE_DRY_MS;
  // Low battery stretches the quiet states only. An active event is still
  // sampled properly, because that is the whole point of the instrument.
  if (lowPower && att < ATT_WATCH) base *= LOW_BATTERY_SLOWDOWN;
  return base;
}

unsigned long power_report_interval(Attention att, bool raining, uint64_t ms_stable) {
  if (ms_stable >= SUSTAINED_AFTER_MS && att != ATT_QUIET && !engine_road_unsafe()) {
    return SUSTAINED_REPORT_MS;      // already inside REPORT_MAX_MS
  }
  unsigned long base;
  if (att >= ATT_EVENT)           base = REPORT_EVENT_MS;
  else if (att == ATT_WATCH)      base = REPORT_RISING_MS;
  else if (raining)               base = REPORT_WET_MS;
  else                            base = REPORT_DRY_MS;
  if (lowPower && att < ATT_WATCH) base *= LOW_BATTERY_SLOWDOWN;
  // The heartbeat must stay inside the gateway offline timeout, otherwise a
  // node saving power on a flat battery is reported as dead. Low battery is
  // expected during monsoon overcast, so this would fire constantly.
  if (base > REPORT_MAX_MS) base = REPORT_MAX_MS;
  return base;
}

bool power_may_sleep(Attention att, bool stalled, bool raining, uint64_t ms_stable) {
#if DEEP_SLEEP_ENABLED
  // Normal and dry: the ordinary quiet case.
  if (att == ATT_QUIET && !raining &&
      power_sample_interval(att, raining, ms_stable) >= DEEP_SLEEP_MIN_MS) {
    return true;
  }
  // A long-stable STALL may also sleep. The condition is stable by definition,
  // the reading has already been sent and archived, and if the gateway has
  // escalated it to a blockage then a blocked drain can wait days for a crew. Without this the node drains itself in
  // under two weeks and loses the very record that proves the blockage.
  //
  // WATERLOGGING deliberately never sleeps: GPIO output is not maintained
  // through deep sleep, so sleeping would extinguish the road warning beacon
  // while the road is still unsafe.
  //
  // engine_road_unsafe() closes the way around that rule. Stall is a
  // combination rather than a state, and SEV_WATERLOGGED with TR_STALLED is a
  // perfectly reachable combination: a flooded road whose water is not moving.
  // Testing the stall flag alone would therefore let the node sleep with the
  // water still over the road. The interlock is on the measured level, not on
  // any label, which is why it survives the split.
  if (stalled && ms_stable >= SUSTAINED_AFTER_MS && !engine_road_unsafe()) {
    return true;
  }
  return false;
#else
  (void)att;
  (void)stalled;
  (void)raining;
  (void)ms_stable;
  return false;
#endif
}

bool power_oled_on(Attention att, uint64_t ms_since_change) {
  if (att != ATT_QUIET) return true;                      // something is happening
  if (ms_since_change < (uint64_t)OLED_ON_AFTER_CHANGE_MS) return true;
  if (clock_now_ms() < oledBootUntilMs) return true;       // cold-boot installation check
  return false;
}
