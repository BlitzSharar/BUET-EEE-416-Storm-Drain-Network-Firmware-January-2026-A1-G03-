// TEST 6: rain plate calibration. Run this ONCE PER PLATE.
//
// Wiring: plate board GND -> GND, AO (analog out) -> GPIO32.
//         VCC -> GPIO13 if you are using switched power (recommended), or to
//         3V3 if you are not. Set RAIN_POWER_PIN in node/config.h to match.
//
// Ignore the blue trimmer pot on the board. It only sets the digital D0 output,
// which this firmware does not use. The threshold is set in software, here.
//
// The sketch walks you through it and prints the two numbers to paste into
// node/config.h. Two numbers, not one: a single threshold chatters when the
// plate sits near it, and the gateway now decides whether a stalled drain is a
// blockage or a storm using rainfall per site, so a chattering plate makes that
// verdict flicker.
//
// Plates are NOT interchangeable. Two from the same bag differ by hundreds of
// counts. Do this for each of the three and label them.
#define RAIN_AO    32
#define RAIN_PWR   13        // set to -1 if the plate VCC goes straight to 3V3
#define SETTLE_MS  10

// Must equal RAIN_ADC_SAMPLES in node/config.h. Thresholds measured on a
// 16-sample average do not apply to a signal read a different way, so
// tests_host/source_invariants_check.sh fails the build if these two drift apart.
#define ADC_SAMPLES 16

// Byte for byte what node/sensor_rain.cpp does, including the absence of a
// delay between samples. If this read is quieter than the node's, the spread
// reported below understates the noise the thresholds actually have to survive.
static int read_plate() {
#if RAIN_PWR >= 0
  digitalWrite(RAIN_PWR, HIGH);
  delay(SETTLE_MS);
#endif
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) sum += analogRead(RAIN_AO);
  int a = (int)(sum / ADC_SAMPLES);   // the node uses this same average
#if RAIN_PWR >= 0
  digitalWrite(RAIN_PWR, LOW);
#endif
  return a;
}

// Watch for seconds_stable of readings that stay inside spread counts, then
// report the average. Stops you writing down a number taken mid-transition.
// Bounded: a plate that never settles reports what it has with a warning rather
// than leaving you staring at a scrolling terminal.
static int settle_and_measure(const char* what, int seconds_stable, int spread,
                              int* out_pp) {
  const unsigned long GIVE_UP_MS = 90000UL;
  unsigned long began = millis();
  Serial.printf("\n%s\n", what);
  Serial.println("  waiting for the reading to settle ...");
  int lo = 4095, hi = 0, ppLo = 4095, ppHi = 0;
  unsigned long stableSince = millis();
  long sum = 0; int n = 0;
  while (true) {
    int v = read_plate();
    if (v < ppLo) ppLo = v;
    if (v > ppHi) ppHi = v;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    if (hi - lo > spread) {            // moved: restart the stability window
      lo = hi = v; stableSince = millis(); sum = 0; n = 0;
      ppLo = ppHi = v;                 // noise measured over the settled run only
    }
    sum += v; n++;
    Serial.printf("    %4d   (spread %d over %lu s)\n", v, hi - lo,
                  (millis() - stableSince) / 1000);
    if (millis() - stableSince >= (unsigned long)seconds_stable * 1000UL && n > 4) {
      int avg = (int)(sum / n);
      Serial.printf("  settled at %d\n", avg);
      *out_pp = ppHi - ppLo;
      return avg;
    }
    if (millis() - began > GIVE_UP_MS) {
      int avg = n > 0 ? (int)(sum / n) : read_plate();
      Serial.printf("  NEVER SETTLED. Taking %d anyway, from %d samples.\n", avg, n);
      Serial.println("  A plate that will not hold still is usually a loose AO");
      Serial.println("  wire or a floating ground. Treat the result as suspect.");
      *out_pp = ppHi - ppLo;
      return avg;
    }
    delay(500);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(RAIN_AO, INPUT);
#if RAIN_PWR >= 0
  pinMode(RAIN_PWR, OUTPUT);
  digitalWrite(RAIN_PWR, LOW);
  Serial.println("\nplate power is SWITCHED on GPIO13");
#else
  Serial.println("\nplate power is CONTINUOUS. Consider switching it: a plate held");
  Serial.println("at DC electrolyses and the tracks corrode away in weeks.");
#endif

  Serial.println("=== RAIN PLATE CALIBRATION ===");
  Serial.println("Three steps. Follow the prompts.");

  int dryPP = 0, wetPP = 0;
  int dry = settle_and_measure(
      "STEP 1 of 3. Plate completely DRY. Wipe it and leave it in still air.",
      8, 40, &dryPP);

  Serial.println("\nSTEP 2 of 3. Now WET the plate the way real rain would.");
  Serial.println("  Sprinkle water across it, do not dunk it and do not use a");
  Serial.println("  finger. A finger is far more conductive than rain and gives");
  Serial.println("  a wet value you will never see outdoors.");
  Serial.println("  You have 20 seconds.");
  for (int i = 20; i > 0; i--) { Serial.printf("  %d\n", i); delay(1000); }

  int wet = settle_and_measure("STEP 3 of 3. Holding wet. Measuring.", 6, 60, &wetPP);

  Serial.println("\n================ RESULT ================");
  Serial.printf("  dry  %4d   (noise %d counts peak to peak)\n", dry, dryPP);
  Serial.printf("  wet  %4d   (noise %d counts peak to peak)\n", wet, wetPP);

  // WORK OUT THE POLARITY RATHER THAN ASSUMING IT.
  //
  // A resistive plate is one half of a voltage divider and which half decides
  // which way AO moves. Plate between AO and ground, wetting it drops its
  // resistance and AO falls. Plate between AO and VCC and AO rises. Both
  // wirings ship on real modules, so this measures which one you have instead
  // of trusting a datasheet picture.
  //
  // Getting it backwards in software is the expensive failure. The node then
  // reports rain when it is dry and dry when it is raining, so the gateway
  // confirms a blockage during every storm and dismisses a real blockage as
  // weather. Neither looks like a wiring fault from the dashboard.
  bool activeHigh = (wet > dry);
  int sep = activeHigh ? (wet - dry) : (dry - wet);

  Serial.printf("  separation %d counts\n", sep);
  Serial.printf("  polarity   the plate reads %s when wet\n",
                activeHigh ? "HIGHER" : "LOWER");
  if (activeHigh) {
    Serial.println("\n  NOTE. This is the less common wiring. The firmware defaults");
    Serial.println("  to the other one, so RAIN_ACTIVE_HIGH below is not optional.");
    Serial.println("  Before you paste it, satisfy yourself that step 1 really was");
    Serial.println("  the dry reading. A swapped pair of measurements looks exactly");
    Serial.println("  like an inverted board from here.");
  }

  if (sep < 400) {
    Serial.println("\n  SEPARATION TOO SMALL. Under about 400 counts the plate");
    Serial.println("  cannot be thresholded reliably. Check the AO wiring, make");
    Serial.println("  sure you wet the plate properly, and if it still reads like");
    Serial.println("  this the plate is probably already corroded. Use another.");
    return;
  }
  if (sep < 800) {
    Serial.println("\n  THIN. It will work, but a healthy plate usually swings well");
    Serial.println("  over a thousand counts between bone dry and properly wet.");
    Serial.println("  Worth wetting it more thoroughly and running this again");
    Serial.println("  before you accept these numbers.");
  }
  if (dry > 3000 && wet > 3000) {
    Serial.println("\n  BOTH READINGS ARE HIGH. Above about 3000 counts the ESP32");
    Serial.println("  ADC is in its compressed non-linear region, so a given");
    Serial.println("  voltage change buys you fewer counts there than lower down.");
    Serial.println("  If the separation is also thin, a larger series resistor on");
    Serial.println("  the divider will move the whole range down and spread it out.");
  }

  // Put the two thresholds either side of the midpoint, a quarter of the
  // separation apart. Wide enough to stop chatter, narrow enough that neither
  // sits near a real reading.
  int mid = (dry + wet) / 2;
  int gap = sep / 4;
  // The floor is 200 because node/config.h REJECTS a narrower gap. It used to
  // be 100, so a plate with a 500 count separation got thresholds 125 apart
  // printed here and then an #error when they were pasted, which is a miserable
  // way to find out. The sketch and the guard now agree.
  if (gap < 200) gap = 200;

  // WET is the threshold crossed going into rain, DRY the one crossed coming
  // out, whichever way round the numbers are.
  int wetThr = activeHigh ? mid + gap / 2 : mid - gap / 2;
  int dryThr = activeHigh ? mid - gap / 2 : mid + gap / 2;

  // Margin is what is left between a threshold and the reading it came from.
  // Too little and normal drift crosses it.
  int margin = (sep - gap) / 2;
  if (margin < 150) {
    Serial.printf("\n  TIGHT. Only %d counts sit between each threshold and the\n", margin);
    Serial.println("  reading it came from, so the plate does not have to drift far");
    Serial.println("  to cross one. Wet it more thoroughly and run this again. A");
    Serial.println("  separation over about 1100 counts gives comfortable margin.");
  }

  // The gap only stops chatter if it is wider than the plate's own noise.
  int worstPP = dryPP > wetPP ? dryPP : wetPP;
  if (worstPP >= gap) {
    Serial.printf("\n  WARNING. This plate wanders by %d counts on its own and the\n", worstPP);
    Serial.printf("  hysteresis gap is only %d, so noise alone can cross it. Widen\n", gap);
    Serial.println("  the two numbers below by hand until they are further apart");
    Serial.println("  than that, or find out why the reading is so unsteady. A");
    Serial.println("  long unshielded AO wire is the usual cause.");
  }

  Serial.println("\n  Paste these into node/config.h for THIS node:");
  Serial.printf("\n    #define RAIN_WET_ADC         %d\n", wetThr);
  Serial.printf("    #define RAIN_DRY_ADC         %d\n", dryThr);
  if (activeHigh) Serial.println("    #define RAIN_ACTIVE_HIGH     1");
#if RAIN_PWR >= 0
  Serial.println("    #define RAIN_POWER_PIN       13");
#endif
  if (activeHigh) {
    Serial.println("\n  ABOVE the wet number it is raining. BELOW the dry number it");
  } else {
    Serial.println("\n  BELOW the wet number it is raining. ABOVE the dry number it");
  }
  Serial.println("  is not. In between the node holds whatever it last decided,");
  Serial.println("  which is what stops it chattering at the boundary.");
  Serial.println("\n  Now verify: run the node firmware and watch the rain field");
  Serial.println("  on the sample line flip 0 to 1 as you wet and dry the plate.");
  Serial.println("========================================");
}

void loop() {
  // Live reading afterwards, so you can sanity check the thresholds by hand.
  Serial.printf("plate: %d\n", read_plate());
  delay(1000);
}
