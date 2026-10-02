#include <Arduino.h>
#include "abs_clock.h"
#include "config.h"
#include "sensor_level.h"

// RTC backed, for the same reason the rule engine is. Deep sleep restarts the
// sketch and clears ordinary statics, and the node sleeps between samples
// whenever the site is dry, which is most of its life. Two things broke:
//
//  1. prevAt was 0 on every wake, so level_rate_mm_min() returned 0 on every
//     dry sample. RATE_WARN_MM_MIN is documented as an independent route into
//     RISING, and while sleeping it never fired at all.
//  2. lastMm was 0 on every wake, so the "no echo, hold the last good value"
//     fallback returned 0 mm instead. Zero depth reads as a dry drain, which
//     is the dangerous direction, and it is reachable: with MOUNT_HEIGHT_MM
//     600 and a 250 mm dead zone there is no valid echo above 350 mm of water.
//
// Timing moved from millis() to the absolute clock for the same reason the
// recession window did: millis() restarts at zero on wake, so a rate computed
// from it across a sleep is arithmetic on two unrelated timebases.
RTC_DATA_ATTR static int lastMm = 0, prevMm = 0;
RTC_DATA_ATTR static uint64_t prevAt = 0, lastAt = 0;
RTC_DATA_ATTR static bool haveReading = false;
static uint8_t lastPings = 0;
static bool lastSampleValid = false;

void level_init() {
  pinMode(PIN_US_TRIG, OUTPUT);
  digitalWrite(PIN_US_TRIG, LOW);
  pinMode(PIN_US_ECHO, INPUT);
}

static long ping_mm_once() {
  digitalWrite(PIN_US_TRIG, LOW);  delayMicroseconds(4);
  // 20 us, not the 10 us used for an HC-SR04. See config.h: the shorter pulse
  // is the single most common reason this sensor appears dead.
  digitalWrite(PIN_US_TRIG, HIGH); delayMicroseconds(US_TRIG_PULSE_US);
  digitalWrite(PIN_US_TRIG, LOW);
  unsigned long us = pulseIn(PIN_US_ECHO, HIGH, US_ECHO_TIMEOUT_US);
  if (us == 0) return -1;                       // no echo, possibly submerged
  long mm = (long)((float)us * 0.1715f);
  // Discard the known minimum-distance glitch. A reading inside the dead zone
  // is physically impossible with a correctly mounted sensor.
  if (mm < US_MIN_VALID_MM) return -1;
  return mm;
}

static void sort_l(long* v, int n) {
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && v[j-1] > v[j]; j--) { long t=v[j]; v[j]=v[j-1]; v[j-1]=t; }
}

// Adaptive median: start with US_MEDIAN_MIN pings. If the readings are tightly
// grouped the water is calm and three is enough. If they are scattered, which
// happens with ripple, foam or floating waste, take more until the spread
// settles or US_MEDIAN_MAX is reached. This spends sensor time and energy only
// when the measurement is actually difficult.
int level_read_mm() {
  long v[US_MEDIAN_MAX];
  int n = 0;
  int attempts = 0;
  lastSampleValid = false;

  while (attempts < US_MEDIAN_MAX) {
    long d = ping_mm_once();
    attempts++;
    if (d > 0) v[n++] = d;

    if (attempts >= US_MEDIAN_MIN && n >= US_VALID_MIN) {
      sort_l(v, n);
      if ((v[n-1] - v[0]) <= US_SPREAD_MM) break;
    }
    if (attempts < US_MEDIAN_MAX) delay(30);
  }
  lastPings = (uint8_t)attempts;
  if (n < US_VALID_MIN) {
    // Blind: no echo returned, because the transducer is submerged, the water
    // is inside the dead zone, or the sensor is unplugged. Holding the last
    // good value is the safe direction once there IS one.
    //
    // Before the first valid echo there is nothing to hold, and there is no
    // safe stand-in either. Returning 0 asserts a dry drain on no evidence;
    // returning LEVEL_HIGH_MM is worse, because the engine cannot tell an
    // invented level from a measured one and will publish BLOCKAGE against a
    // named drain about twenty minutes later. So this returns 0 and
    // level_have_reading() stays false, and node.ino declines to run the
    // engine at all until a real reading arrives.
    return lastMm;
  }

  sort_l(v, n);
  long dist = v[n/2];
  int depth = (int)(MOUNT_HEIGHT_MM - dist);
  if (depth < 0) depth = 0;

  prevMm = lastMm; prevAt = lastAt;
  lastMm = depth;  lastAt = clock_now_ms();
  haveReading = true;
  lastSampleValid = true;
  return depth;
}

int level_last_mm() { return lastMm; }
uint8_t level_last_pings() { return lastPings; }
bool level_have_reading() { return haveReading; }
bool level_last_sample_valid() { return lastSampleValid; }

// Rate is computed from the real elapsed time between samples, which matters
// now that the sampling interval changes with state.
int level_rate_mm_min() {
  if (prevAt == 0 || lastAt <= prevAt) return 0;
  float dt_min = (float)(lastAt - prevAt) / 60000.0f;
  if (dt_min < 0.001f) return 0;
  // Guard against an implausibly long gap. If the node has been asleep for
  // hours, dividing a real level change by that gap gives a meaninglessly
  // small rate that says nothing about how fast the water is moving now.
  if (dt_min > 30.0f) return 0;
  return (int)((float)(lastMm - prevMm) / dt_min);
}
