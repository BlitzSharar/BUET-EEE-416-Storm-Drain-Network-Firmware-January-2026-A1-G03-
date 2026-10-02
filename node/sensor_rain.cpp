#include <Arduino.h>
#include "abs_clock.h"
#include "config.h"
#include "sensor_rain.h"

static volatile uint32_t tips = 0;

// RTC backed so the latch survives deep sleep. Without it every wake would
// restart at "dry", which is the direction that turns a stall into a blockage.
RTC_DATA_ATTR static bool wetLatched = false;
RTC_DATA_ATTR static int  lastAdc    = 4095;

// RTC backed. Nodes 2 and 3 have no rain sensor and rely entirely on the
// gateway broadcast, and deep sleep clears ordinary statics. Without this the
// rain flag reset to false on every wake, so a node could sleep through the
// start of rainfall and, worse, evaluate the blockage rule believing it was
// dry while it was actually raining. The last known rainfall state is the
// correct thing to carry across a sleep.
RTC_DATA_ATTR static bool     bcastRaining = false;
RTC_DATA_ATTR static uint8_t  bcastMm10    = 0;
RTC_DATA_ATTR static bool     bcastHave    = false;
RTC_DATA_ATTR static uint64_t bcastAtMs    = 0;

#if NODE_HAS_RAIN
static void IRAM_ATTR onTip() { tips++; }
#endif

void rain_init() {
#if NODE_HAS_RAIN
  pinMode(PIN_RAIN_ANALOG, INPUT);
#if RAIN_POWER_PIN >= 0
  pinMode(RAIN_POWER_PIN, OUTPUT);
  digitalWrite(RAIN_POWER_PIN, LOW);   // unpowered between readings
#endif
  pinMode(PIN_RAIN_PULSE, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RAIN_PULSE), onTip, FALLING);
#endif
}

RainInput rain_get() {
#if NODE_HAS_RAIN
  RainInput r;
#if RAIN_POWER_PIN >= 0
  // Power the plate only for the reading. Continuous DC across the electrodes
  // electrolyses the water bridging them and eats the tracks; a few
  // milliseconds per sample does not.
  digitalWrite(RAIN_POWER_PIN, HIGH);
  delay(RAIN_SETTLE_MS);
#endif
  // Average, do not spot read. A single analogRead on this ADC scatters by tens
  // of counts on a plate that is not changing, and on a poor plate the two
  // thresholds are only about 100 counts apart. tests/06_rain averages the same
  // number when it measures the thresholds, so the node thresholds a signal with
  // the same noise as the one they were derived from.
  long sum = 0;
  for (int i = 0; i < RAIN_ADC_SAMPLES; i++) sum += analogRead(PIN_RAIN_ANALOG);
  int a = (int)(sum / RAIN_ADC_SAMPLES);     // resistive board: lower = wetter
#if RAIN_POWER_PIN >= 0
  digitalWrite(RAIN_POWER_PIN, LOW);
#endif

  // Hysteresis. A single threshold chatters when the plate sits near it, and
  // the gateway now decides blockage against rainfall per site, so a chattering
  // plate makes the verdict flicker. Between the two thresholds, hold.
  //
  // The comparison direction follows RAIN_ACTIVE_HIGH, because which half of
  // the divider the plate forms decides whether AO rises or falls when it gets
  // wet, and both wirings exist on shipping modules. config.h enforces that the
  // two thresholds are ordered to match.
#if RAIN_ACTIVE_HIGH
  if (a > RAIN_WET_ADC)      wetLatched = true;
  else if (a < RAIN_DRY_ADC) wetLatched = false;
#else
  if (a < RAIN_WET_ADC)      wetLatched = true;
  else if (a > RAIN_DRY_ADC) wetLatched = false;
#endif
  r.raining = wetLatched;
  lastAdc = a;
  r.mm_x10  = 0;                             // TODO: tips * MM_PER_TIP if bucket fitted
  r.fromBroadcast = false;
  return r;
#else
  // A plate-less build takes rainfall from the gateway broadcast, and that
  // value has to EXPIRE. rain_context_valid() existed to say when, and nothing
  // called it, so a retained broadcast was believed for ever and then
  // transmitted back in byte 5, where the gateway credited it as this site's
  // own plate reading. A guard nothing enforces is worse than no guard, because
  // the comment on RAIN_BCAST_TTL_MS reads as though it is enforced.
  RainInput r;
  bool fresh = rain_context_valid();
  r.raining = fresh && bcastRaining;
  r.mm_x10  = fresh ? bcastMm10 : 0;
  r.fromBroadcast = true;
  return r;
#endif
}

void rain_set_broadcast(bool raining, uint8_t mm_x10) {
  bcastRaining = raining;
  bcastMm10 = mm_x10;
  bcastAtMs = clock_now_ms();
  bcastHave = true;
}

bool rain_context_valid() {
#if NODE_HAS_RAIN
  return true;
#else
  uint64_t now = clock_now_ms();
  return bcastHave && now >= bcastAtMs &&
         (now - bcastAtMs) <= (uint64_t)RAIN_BCAST_TTL_MS;
#endif
}

int rain_last_adc() { return lastAdc; }
