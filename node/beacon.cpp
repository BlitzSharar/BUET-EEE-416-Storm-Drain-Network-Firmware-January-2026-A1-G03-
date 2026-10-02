#include <Arduino.h>
#include "config.h"
#include "beacon.h"

// Neither output here has a feedback path. A radio that fails shows up at the
// gateway, but a silent buzzer looks exactly like a buzzer that was never asked
// to sound, and so does a dark beacon.
//
// The buzzer got that treatment first, after defect 11: a passive piezo driven
// as an active one, silent on the bench through 88 correct commands. It has a
// polarity option, a drive type option, a self test at boot and a pure pattern
// function a host test can drive.
//
// The beacon had a pin number and an inline digitalWrite, which made it the
// only output rule in the project with no test behind it and no way to tell a
// wiring fault from a rule that never fired. It is the output a member of the
// public actually sees. It now has the same four things, for the same reason.

static Severity lastSev = SEV_NORMAL;   // so beacon_tick() can re-apply it

static void beacon_drive(bool on) {
#if NODE_HAS_BEACON
  #if BEACON_ACTIVE_LOW
    digitalWrite(PIN_BEACON, on ? LOW : HIGH);
  #else
    digitalWrite(PIN_BEACON, on ? HIGH : LOW);
  #endif
#else
  (void)on;
#endif
}

static void buzzer_drive(bool on) {
#if BUZZER_PASSIVE
  // A bare piezo needs an alternating signal. LEDC generates it in hardware, so
  // the tone survives a loop that is busy doing something else.
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_BUZZER, on ? 128 : 0);
  #else
    ledcWrite(BUZZER_LEDC_CH, on ? 128 : 0);
  #endif
#else
  #if BUZZER_ACTIVE_LOW
    digitalWrite(PIN_BUZZER, on ? LOW : HIGH);
  #else
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  #endif
#endif
}

void beacon_init() {
#if NODE_HAS_BEACON
  pinMode(PIN_BEACON, OUTPUT);
  beacon_drive(false);
  #if BEACON_SELFTEST
  // Two flashes, from the production firmware through the production drive
  // path. If you see these then the pin, the wiring and the polarity are all
  // correct and a later dark beacon is the RULE, not the hardware. If you do
  // not see them, stop and fix the wiring before running any trial, because
  // nothing later in the run will tell you.
  Serial.println("beacon self test: two flashes now");
  for (int i = 0; i < 2; i++) {
    beacon_drive(true);  delay(200);
    beacon_drive(false); delay(200);
  }
  #endif
#endif

#if BUZZER_PASSIVE
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_BUZZER, BUZZER_FREQ_HZ, 8);
  #else
    ledcSetup(BUZZER_LEDC_CH, BUZZER_FREQ_HZ, 8);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CH);
  #endif
#else
  pinMode(PIN_BUZZER, OUTPUT);
#endif
  buzzer_drive(false);

#if BUZZER_SELFTEST
  // One chirp, from inside the production firmware, using the production drive
  // path. If you hear this at boot then the pin, the polarity and the drive
  // type are all correct and a later silence is the RULE not the wiring. If you
  // do not hear it, stop and fix the wiring before calibrating anything.
  Serial.println("buzzer self test: one chirp now");
  buzzer_drive(true);
  delay(150);
  buzzer_drive(false);
#endif
}

void beacon_set(Severity sev) {
  lastSev = sev;
  beacon_drive(beacon_should_light(sev, millis()));
  // SAY WHEN THE PATTERN CHANGES, not when the pin does.
  //
  // This used to print on every edge. That was survivable at two chirps every
  // ten seconds and is not any more: ELEVATED beeps once a second, so it would
  // put two lines a second into the log and bury the sample lines you are
  // actually reading. WATERLOGGED is worse in the other direction, a single
  // edge and then nothing for hours, which reads as the buzzer having stopped.
  //
  // What is worth knowing is which of the three patterns is running, and that
  // changes a handful of times in a whole trial.
#if BRINGUP_MODE
  static int lastPattern = -1;
  int pattern = (sev >= SEV_WATERLOGGED) ? 2 : (sev >= SEV_ELEVATED ? 1 : 0);
  if (pattern != lastPattern) {
    Serial.println(pattern == 2 ? "buzzer: CONTINUOUS (road under water)"
                 : pattern == 1 ? "buzzer: beeping once a second (elevated)"
                                : "buzzer: silent");
    // SAY WHAT THE BEACON WAS TOLD TO DO.
    //
    // The beacon had no serial line at all. The argument was that a beacon
    // that fails is visible, which is true only of somebody standing in front
    // of it. On the bench it means a dark beacon and a wrong pin look
    // identical, and in the archive afterwards there is nothing at all. This
    // says what the firmware commanded, so a beacon that disagrees with this
    // line is wiring and a beacon that agrees with it is the rule.
  #if NODE_HAS_BEACON
    Serial.println(pattern == 2 ? "beacon: SOLID (road under water)"
                 : pattern == 1 ? "beacon: blinking (elevated)"
                                : "beacon: dark");
  #endif
    lastPattern = pattern;
  }
#endif

  // Two patterns, one function, defined in beacon.h so the test can drive the
  // same rule the firmware runs. Beeping at knee depth, continuous once the
  // road is under water.
  //
  // The pattern is computed from millis() at the instant of the call and the
  // pin then holds that value until the next call, so this function has to be
  // called often. node.ino calls it from the bottom of loop() on every pass
  // rather than once per sample cycle, which is what keeps the 150 ms beeps
  // from being missed entirely. The continuous case does not care, since LEDC
  // holds the tone in hardware through anything that blocks the loop.
  buzzer_drive(buzzer_should_sound(sev, millis()));
}

void beacon_tick() { beacon_set(lastSev); }
