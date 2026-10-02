#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
// BRING-UP MODE.  Set to 1 while you are proving the link, 0 for real use.
//
// With this at 1 the node samples every 2 seconds, reports every 10, never
// deep sleeps, and prints a line on every sample so you can see it is alive
// even before the radio works. That turns a 15 minute wait into a 10 second
// one while leaving all the decision logic identical: only the clock changes.
//
// TURN IT OFF BEFORE ANY REAL MEASUREMENT.  With it on the node draws about
// 60 mA continuously, flattens a battery in roughly two days, and the
// recession window timing in the blockage rule is meaningless because the
// node is sampling far faster than a drain can empty.
// =====================================================================
#ifndef BRINGUP_MODE
#define BRINGUP_MODE       0
#endif

// ---------------- Node identity ----------------
#ifndef NODE_ID
#define NODE_ID            1      // 1 Intersection, 2 Low-lying road, 3 Outfall
#endif
// Every node carries a resistive rain plate. It used to be node 1 only, with
// nodes 2 and 3 taking rainfall from a gateway broadcast, and that dependency
// was the single worst failure mode in the system: with the gateway off air
// their blockage rule was silently disabled and nothing said so.
//
// Note what this does NOT do. The node still takes no rain input into its
// recession rule. It reports its own rainfall and reports that water is not
// going down, and the gateway combines the two across all three sites. Gating
// at the node would suppress the stall during rain, and the gateway would then
// never get to see the one case worth seeing: one drain stalled while its
// neighbours drain in the same downpour.
#ifndef NODE_HAS_RAIN
#define NODE_HAS_RAIN      1
#endif
#define NODE_HAS_BEACON    (NODE_ID == 2)

#if BRINGUP_MODE != 0 && BRINGUP_MODE != 1
#error "BRINGUP_MODE must be 0 or 1"
#endif

#if NODE_ID < 1 || NODE_ID > 3
#error "NODE_ID must be 1, 2 or 3"
#endif

// ---------------- Pin map (final proposal, section 3.5) ----------------
#define PIN_LORA_SCK       18
#define PIN_LORA_MISO      19
#define PIN_LORA_MOSI      23
#define PIN_LORA_NSS       5
#define PIN_LORA_RESET     14
#define PIN_LORA_DIO0      26
#define PIN_US_TRIG        25
#define PIN_US_ECHO        34     // input only; via 5V to 3.3V divider
#define PIN_OLED_SDA       21
#define PIN_OLED_SCL       22
#define PIN_RAIN_ANALOG    32     // resistive rain sensor (ADC1)
#define PIN_RAIN_PULSE     27     // tipping bucket interrupt (if fitted)
#define PIN_BEACON         33     // beacon driver (GPIO2 avoided: boot strap pin)
#define PIN_BUZZER         4

// ---------------- Buzzer wiring, which varies by module ----------------
// Two things differ between the parts sold as "buzzer" and neither is visible
// from the outside. Run tests/08_buzzer to find out which you have.
//
// BUZZER_ACTIVE_LOW. Three pin modules with a transistor on board are often
// wired so that pulling the I/O pin LOW sounds the buzzer. Get this wrong and
// the buzzer sounds continuously while the node is idle and goes SILENT during
// the alert, which is the exact opposite of what you want and reads from the
// outside as a buzzer that does not work.
#ifndef BUZZER_ACTIVE_LOW
#define BUZZER_ACTIVE_LOW    0
#endif
//
// BUZZER_PASSIVE. An active buzzer has an oscillator inside and sounds on a
// steady level. A passive piezo is a bare disc and needs an alternating signal,
// so a steady level makes it tick once and then nothing. The ESP32 LEDC
// peripheral generates the square wave in hardware.
//
// SET TO 1, because the rigs use passive piezos. This was 0 and the symptom was
// exactly what you would expect and not at all what it looked like: the serial
// line printed "buzzer ON" 88 times in one run and nothing was audible, because
// a steady level on a bare disc is a single faint tick. The firmware was
// working and the hardware was working and they did not match.
#ifndef BUZZER_PASSIVE
#define BUZZER_PASSIVE       1
#endif
#define BUZZER_FREQ_HZ       2700
#define BUZZER_LEDC_CH       0

// THE TWO ALERT PATTERNS, as an on-time and a period each.
//
//   ELEVATED    knee deep. Beeps once a second. Caution, the road is passable.
//   WATERLOGGED road under water. Continuous tone, no gap at all.
//
// Continuous falls out of the same formula when the on-time equals the period,
// so there is one rule and no special case to keep in step with the other one.
//
// The cost is worth stating rather than discovering. A continuous tone is
// roughly 30 mA for as long as the water is over the road, and a blocked drain
// stays that way for as long as it takes somebody to clear it. The node is
// already awake at that point and not sleeping, so it is about a 40 percent
// increase on an already-awake node rather than a new load, but over a
// multi-day blockage it is real. The alert being unmistakable was judged worth
// more than the battery, which is a defensible call for a road hazard and is
// recorded here as a decision rather than left as an accident.
//
// To back it off later, give the waterlog pattern a shorter on-time than its
// period. Nothing else has to change.
#ifndef BUZZ_ELEVATED_ON_MS
#define BUZZ_ELEVATED_ON_MS      150
#endif
#ifndef BUZZ_ELEVATED_PERIOD_MS
#define BUZZ_ELEVATED_PERIOD_MS  1000
#endif
#ifndef BUZZ_WATERLOG_ON_MS
#define BUZZ_WATERLOG_ON_MS      1000
#endif
#ifndef BUZZ_WATERLOG_PERIOD_MS
#define BUZZ_WATERLOG_PERIOD_MS  1000
#endif

#if BUZZ_ELEVATED_PERIOD_MS == 0 || BUZZ_WATERLOG_PERIOD_MS == 0
#error "A buzzer period of zero divides by zero in buzzer_should_sound()"
#endif
#if BUZZ_ELEVATED_ON_MS > BUZZ_ELEVATED_PERIOD_MS || \
    BUZZ_WATERLOG_ON_MS > BUZZ_WATERLOG_PERIOD_MS
#error "A buzzer on-time longer than its period is just a continuous tone written confusingly"
#endif
#if BUZZ_ELEVATED_ON_MS >= BUZZ_ELEVATED_PERIOD_MS
#error "ELEVATED is the caution pattern and must beep. Equal on-time and period is continuous"
#endif
//
// A short chirp at boot, so you can hear from the REAL firmware that the pin,
// the wiring and the polarity are all correct, before spending an afternoon
// wondering why an alert is silent. Costs 150 ms once per boot.
#ifndef BUZZER_SELFTEST
#define BUZZER_SELFTEST      1
#endif

#if BUZZER_ACTIVE_LOW && BUZZER_PASSIVE
#error "BUZZER_ACTIVE_LOW has no meaning for a passive piezo: LEDC drives both edges"
#endif

// ---------------- Beacon ----------------
//
// The beacon had a pin number and nothing else while the buzzer had a polarity
// option, tunable timing, a boot self test and a pure function a host test can
// drive. That asymmetry is the whole reason the buzzer's fault was caught and
// the beacon's would not have been. Defect 11 was a passive piezo driven as an
// active one, silent through 88 correct commands, and the beacon has been
// carrying the same shape of risk since.
//
// BEACON_ACTIVE_LOW. CONFIRMED 21 September: this build uses a bare LED, so 0
// is correct and nothing needs changing.
//
// The option stays because the alternative fails silently. A driver module or a
// transistor stage with the LED on the collector lights on LOW, and getting
// that wrong inverts the warning: dark when the road is under water, lit when
// it is clear. The serial monitor would report the pattern correctly the whole
// time, because the firmware would be right and only the last millimetre of
// wiring wrong. If the beacon is ever rebuilt behind a driver, set this to 1.
#ifndef BEACON_ACTIVE_LOW
#define BEACON_ACTIVE_LOW    0
#endif

// A bare LED on GPIO33 needs a series resistor and the ESP32 will not remind
// you. 220R gives roughly 6 mA with a red LED, which is bright enough indoors
// and well inside the 12 mA a GPIO drives comfortably. Do not go below 150R.

// The caution blink. WATERLOGGED is solid and takes no period, for the same
// reason the buzzer's continuous case needs no special branch.
#ifndef BEACON_BLINK_PERIOD_MS
#define BEACON_BLINK_PERIOD_MS   1000
#endif
#ifndef BEACON_BLINK_ON_MS
#define BEACON_BLINK_ON_MS        500
#endif

#if BEACON_BLINK_PERIOD_MS == 0
#error "A beacon period of zero divides by zero in beacon_should_light()"
#endif
#if BEACON_BLINK_ON_MS >= BEACON_BLINK_PERIOD_MS
#error "ELEVATED is the caution pattern and must blink. Equal on-time and period is solid, which is the WATERLOGGED pattern and would make the two indistinguishable"
#endif

// Two flashes at boot, from the production firmware through the production
// drive path, so the pin, the wiring and the polarity are all proven before
// anybody relies on the beacon during an event. Two rather than one so it
// cannot be confused with a power LED coming on.
#ifndef BEACON_SELFTEST
#define BEACON_SELFTEST      1
#endif

#define PIN_VBAT           35     // input only; battery divider (ADC1)

// ---------------- Radio ----------------
#define LORA_FREQ_HZ       433000000L  // confirm permitted band with BTRC before deployment
#define LORA_SYNC_WORD     0x37
#define LORA_SF_DEFAULT      8       // tune from the week 5 range test
// SPREADING FACTOR CEILING. Airtime roughly doubles per SF step, so this is a
// legal limit and not a preference.
//
// Three nodes on the 30 s event cadence, plus the gateway's periodic broadcast,
// against a 1 percent duty cycle of 36 s per hour:
//
//     SF7    17.5 s/h    49 percent of the budget
//     SF8    30.5 s/h    85 percent
//     SF9    60.3 s/h   167 percent      ILLEGAL
//
// This said "SF7 to SF9 keeps the network legal" and that was simply wrong: SF9
// is nearly twice the budget, and SF8 was over it too while the gateway was
// still replying to every uplink. tests_host/timing_test.cpp now computes the
// figure from LORA_SF_DEFAULT, LORA_SF_MAX, REPORT_EVENT_MS and NUM_NODES and
// fails the suite if the shipping configuration does not fit, so this table
// cannot go stale the way the sentence it replaces did.
//
// If the week 5 range test says you need SF9, the fix is REPORT_EVENT_MS, not
// this ceiling: at a 60 s event cadence SF9 comes to 94 percent, and at 90 s to
// 70 percent. Raise one and re-run the suite. Choose the lowest SF that gives
// an adequate link margin.
#define LORA_SF_MAX          8
#define BCAST_ID           0xFF

#if LORA_SF_DEFAULT < 6 || LORA_SF_DEFAULT > LORA_SF_MAX
#error "LORA_SF_DEFAULT must be between 6 and LORA_SF_MAX"
#endif

// ================= ADAPTIVE SAMPLING (design review response) =================
// Sampling and reporting rates are selected by state and rainfall, not fixed.
// Rationale: a dry drain carries no information worth spending energy on, while
// a rising drain is under-sampled at a fixed 10 s / 60 s cadence. See the power
// budget in the project documentation for the measured effect.

#if BRINGUP_MODE
// Bring-up timings. Short enough to watch, identical logic underneath.
#define SAMPLE_DRY_MS         2000UL
#define SAMPLE_WET_MS         2000UL
#define SAMPLE_RISING_MS      2000UL
#define SAMPLE_EVENT_MS       2000UL

#define REPORT_DRY_MS        10000UL   // a packet every 10 s, even doing nothing
#define REPORT_WET_MS        10000UL
#define REPORT_RISING_MS     10000UL
#define REPORT_EVENT_MS      10000UL
#else
#define SAMPLE_DRY_MS        60000UL   // Normal, no rain: watch slowly
#define SAMPLE_WET_MS        10000UL   // Normal, raining: watch normally
#define SAMPLE_RISING_MS      5000UL   // Rising: watch closely
#define SAMPLE_EVENT_MS       5000UL   // Waterlogging or Blockage

#define REPORT_DRY_MS       900000UL   // 15 min heartbeat
#define REPORT_WET_MS       300000UL   // 5 min
#define REPORT_RISING_MS     60000UL   // 1 min
#define REPORT_EVENT_MS      30000UL   // 30 s, plus immediate on any state change
#endif

// Hard ceiling on the reporting interval. Low battery stretches the quiet
// intervals, but the heartbeat must never exceed the gateway offline timeout
// (NODE_TIMEOUT_MS, 20 min) or a power saving node is reported as failed.
#if BRINGUP_MODE
#define REPORT_MAX_MS        10000UL
#else
#define REPORT_MAX_MS       900000UL   // 15 min, matching the gateway timeout margin
#endif

// SUSTAINED STATE BACKOFF.
// A blocked drain is exactly the condition that persists: maintenance may take
// days. Reporting every 30 s and never sleeping for all that time drains the
// node in under two weeks, so the node that found the blockage goes silent and
// takes the recession record with it. Once a state has been stable for this
// long it carries no new information at the fast rate, so the node backs off.
// Any change still transmits immediately, so responsiveness is unaffected.
#if BRINGUP_MODE
#define SUSTAINED_AFTER_MS   0xFFFFFFFFUL   // never, during bring-up
#else
#define SUSTAINED_AFTER_MS   (30UL * 60UL * 1000UL)   // 30 min of no change
#endif
#define SUSTAINED_SAMPLE_MS  60000UL                   // then sample once a minute
#define SUSTAINED_REPORT_MS  600000UL                  // and report every 10 min

// Randomised jitter added to every transmission. Without it, nodes on the same
// fixed period can lock into a repeating collision and drop the same packet
// every cycle. See the interference analysis in the documentation.
#define TX_JITTER_MS          3000UL

// Deep sleep is used only in the dry state, where a 60 s gap is acceptable.
// In every other state the node stays awake so it can react within one sample.
#if BRINGUP_MODE
#define DEEP_SLEEP_ENABLED   0   // sleeping drops the USB serial link and looks like a crash
#else
#define DEEP_SLEEP_ENABLED   1
#endif
#define DEEP_SLEEP_MIN_MS   30000UL    // never sleep for less than this

// OLED is a 15 mA load. It is off unless something is happening.
#define OLED_ON_AFTER_CHANGE_MS  120000UL   // 2 min after any state change
#define OLED_ON_AT_BOOT_MS        60000UL

// ---------------- Adaptive median filter ----------------
#define US_MEDIAN_MIN        3     // stable water: 3 pings is enough
#define US_MEDIAN_MAX        7     // scattered readings: take more
#define US_VALID_MIN         2     // never trust a single isolated echo
#define US_SPREAD_MM        15     // spread above this triggers extra pings

#if US_VALID_MIN < 2 || US_VALID_MIN > US_MEDIAN_MIN || US_MEDIAN_MIN > US_MEDIAN_MAX
#error "Ultrasonic median limits are inconsistent"
#endif

// ---------------- Timing ----------------
#if BRINGUP_MODE
#define TX_FAILURE_RETRY_MS  5000UL
#else
#define TX_FAILURE_RETRY_MS 30000UL
#endif

// DOES THIS NODE NEED THE GATEWAY'S RAINFALL REPLY AT ALL?
//
// Only a node built without its own plate does. A node that measures its own
// rainfall has nothing to wait for, and waiting costs real power: the window
// below holds the receiver on for 1500 ms after every single report, which in
// the 30 s event state is five percent of the node's life spent listening for
// a packet that is not coming. The gateway stopped sending it by default for
// its own reasons, GW_REPLY_TO_EVERY_UPLINK in gateway/config.h.
//
// Derived rather than set, so the two cannot disagree: a node with a plate does
// not listen, a node without one does.
#define NODE_EXPECTS_GW_REPLY  (!NODE_HAS_RAIN)

// Listen window after each transmission, for that reply. Must exceed the
// gateway turnaround plus the downlink time on air. At SF12 a 5 byte downlink
// is about 830 ms, so 1200 ms was marginal and any gateway delay broke it.
// 1500 ms holds even at SF12 provided the gateway replies before doing its slow
// work, which it does. See tests_host/timing_test.cpp.
#define RX_WINDOW_MS         1500UL

// ---------------- Thresholds: CALIBRATE PER SITE on the bench rig ----------------
#define LEVEL_SAFE_MM        150      // TODO per site
#define LEVEL_HIGH_MM        120      // set 19 Sep from the bench rig

// THE ELEVATED BAND IS NOW ONLY 30 mm WIDE, AND THAT IS THE SAME NUMBER AS
// RECEDE_DROP_MM. Worth knowing before you read a trial and think it is wrong.
//
// A drain that falls the full RECEDE_DROP_MM from just under LEVEL_SAFE_MM
// lands at 119 mm, which is NORMAL. So on this rig a proven recession will
// usually take the node straight out of ELEVATED rather than leaving it there
// clearing, and ELEVATED CLEARING becomes a narrow, short-lived readout.
//
// Nothing breaks. The recession timer re-anchors on the drop and the severity
// machine follows the level, exactly as before. It only means the band you will
// actually watch a stall develop in is 30 mm tall, so the vessel needs to be
// filled and held with more care than it did at 50 mm.
//
// The band is guarded against the trend deadband rather than a taste value,
// but that guard cannot live here. It needs TREND_DEADBAND_MM, which is defined
// further down this file, and an undefined identifier inside #if evaluates to
// ZERO rather than failing. Written here it read as an enforced rule and was
// inert: setting the band to 4 mm compiled clean. It now sits immediately after
// TREND_DEADBAND_MM, where both halves of the comparison exist.
#define RATE_WARN_MM_MIN     20       // TODO per site
// THE RECESSION WINDOW SCALES WITH BRING-UP, like every other interval.
//
// It did not, and that was a trap. Bring-up takes the sampling cadence from
// 60 s to 2 s, but the recession window stayed at twenty minutes, so a single
// stall trial still took twenty minutes plus the debounce. Ten stalled trials
// and ten draining ones is seven hours of watching a bucket.
//
// The calibration guide said to shorten it by hand and put it back afterwards.
// That is a manual step in the middle of a long bench session, and forgetting
// the second half means every field measurement afterwards is taken against a
// two minute window while the report claims twenty. Tying it to BRINGUP_MODE
// makes reverting it the same single switch as everything else.
//
// The rule itself is untouched. Only the clock changes, which is the whole
// contract of bring-up mode.
#if BRINGUP_MODE
#define RECEDE_WINDOW_MS     (2UL * 60UL * 1000UL)    // 2 min, so a trial is watchable
#else
#define RECEDE_WINDOW_MS     (20UL * 60UL * 1000UL)   // TODO per site
#endif

// RECEDE_DROP_MM IS THE SAME IN BOTH MODES, and that is deliberate.
//
// It was briefly 15 on the bench, on the reasoning that a bucket empties faster
// than a drain. That was wrong twice. It is a DISTANCE, and the depth axis does
// not scale with bring-up: LEVEL_HIGH_MM, LEVEL_SAFE_MM and MOUNT_HEIGHT_MM are
// all unchanged, so a millimetre means the same thing in both modes. And 15 mm
// is only 1.5 times the sensor error, against the three times margin this value
// is sized for, so still water alone could re-anchor the timer and a stall
// would take twice as long to declare. Halving it also put it below
// 2 x TREND_DEADBAND_MM, so one trend window of movement could reset the timer.
//
// The honest consequence of holding it fixed while the window compresses 10x is
// that the bench rate threshold is not the field rate threshold. The field
// calls a drain stalled below 30 mm in 20 min, which is 1.5 mm/min. The bench
// calls it stalled below 30 mm in 2 min, which is 15 mm/min. There is no value
// that preserves both the rate and the noise margin under 10x compression, and
// the noise margin is the one that decides whether the rule works at all.
//
// So the bench proves the RULE fires when water is held and does not fire when
// it drains. It does not measure the field's rate threshold. Set that from the
// real drain's emptying time, as Step 4 says.
#define RECEDE_DROP_MM       30       // TODO per site
#define DEBOUNCE_SAMPLES     3

// ---------------- Trend detection ----------------
// The window is a DURATION, not a sample count. The sampling interval changes
// with state, so a deadband applied to N samples would mean 60 mm/min at the
// bring-up cadence and 2 mm/min at the dry cadence, making the trend field most
// sensitive exactly when nothing is happening.
#if BRINGUP_MODE
#define TREND_WINDOW_MS      10000UL    // 10 s, so a demonstration stays watchable
#else
#define TREND_WINDOW_MS      60000UL    // 1 min
#endif

// Movement smaller than this across the window counts as STEADY. Set from
// measurement rather than taste: across ten still water stretches in the
// 18 September capture, spanning nine different levels from 0 to 239 mm, the
// worst span seen over a window was 5 mm and the 95th percentile was 4 mm.
// 8 mm clears the worst observed noise by 1.6 times.
#define TREND_DEADBAND_MM    8

#if TREND_DEADBAND_MM < 6
#error "TREND_DEADBAND_MM is at or below the measured sensor noise floor; still water will flap between STEADY and RISING"
#endif

// THE ELEVATED BAND, GUARDED HERE BECAUSE BOTH VALUES EXIST BY THIS LINE.
//
// A band narrower than two trend deadbands could be crossed by sensor noise
// alone, and severity would then chatter across a threshold that drives the
// buzzer, the beacon and the road-unsafe interlock.
//
// This guard was first written up beside LEVEL_HIGH_MM, sixty lines above
// TREND_DEADBAND_MM. An undefined identifier inside #if evaluates to zero
// rather than failing, so the test read `band < 0`, which is never true. A 4 mm
// band compiled clean. Position is part of a preprocessor guard's correctness,
// not a matter of where it reads best.
#if (LEVEL_SAFE_MM - LEVEL_HIGH_MM) < (2 * TREND_DEADBAND_MM)
#error "The ELEVATED band is narrower than two trend deadbands, so sensor noise alone could cross it. Widen the gap between LEVEL_HIGH_MM and LEVEL_SAFE_MM."
#endif

// THE DEADBAND IS A RATE, NOT A DISTANCE, AND THE CODE HAS TO KNOW THAT.
//
// The window closes at the first sample at or after TREND_WINDOW_MS, so when
// the sampling interval is LONGER than the window the comparison actually spans
// the sampling interval. A fixed 8 mm across 180 s is 2.67 mm/min, against
// 8 mm/min at every faster cadence. power.cpp triples the dry interval on low
// battery, 60 s to 180 s, so the trend field became three times more sensitive
// in the quietest state on the weakest battery. That is precisely the inversion
// the duration window was introduced to remove, reappearing by another route.
//
// status_engine.cpp scales the deadband by the time actually elapsed, so the
// threshold is a constant 8 mm per TREND_WINDOW_MS whatever the cadence.
//
// Past this multiple of the window, two readings are too far apart for the
// difference between them to describe a trend at all. The engine re-anchors and
// reports STEADY rather than inventing a verdict. 4 covers every shipping
// cadence: the longest is SAMPLE_DRY_MS tripled on low battery, which is 3x.
// Only an abnormal gap, a reset or a watchdog recovery, reaches it.
#define TREND_STALE_FACTOR   4

// The guard below used to be "#if TREND_STALE_FACTOR < 3", which compared a
// constant against a hand-copied 3 and could see neither SAMPLE_DRY_MS nor the
// multiplier that was written out as a bare literal in power.cpp. Changing that
// literal to 5, a plausible power tweak, pushed the dry cadence past the cutoff,
// and then EVERY trend evaluation in the dry state on a low battery landed in
// the "too far apart" branch and reported STEADY for ever. A 12 mm/min rise
// becomes invisible and the cadence never escalates, silently, with the whole
// test suite green.
//
// So derive it instead. These two are the multipliers power.cpp applies when
// the battery is low, named here so this guard can actually see them, and
// SAMPLE_LONGEST_MS is the slowest the node can ever sample. The cutoff has to
// be at least that, or the slowest cadence can never produce a trend at all.
#ifndef LOW_BATTERY_SLOWDOWN
#define LOW_BATTERY_SLOWDOWN 3
#endif
#ifndef SUSTAINED_LOW_BATTERY_SLOWDOWN
#define SUSTAINED_LOW_BATTERY_SLOWDOWN 2
#endif
#if BRINGUP_MODE
// SUSTAINED_AFTER_MS is 0xFFFFFFFF in bring-up, so the sustained cadence is
// unreachable and must not be counted here. Including it made this guard fire
// on a perfectly valid bring-up build, which is how a guard gets deleted
// instead of understood.
#define SAMPLE_LONGEST_MS    (SAMPLE_DRY_MS * LOW_BATTERY_SLOWDOWN)
#else
#define SAMPLE_LONGEST_MS \
  ((SAMPLE_DRY_MS * LOW_BATTERY_SLOWDOWN) > \
   (SUSTAINED_SAMPLE_MS * SUSTAINED_LOW_BATTERY_SLOWDOWN) \
     ? (SAMPLE_DRY_MS * LOW_BATTERY_SLOWDOWN) \
     : (SUSTAINED_SAMPLE_MS * SUSTAINED_LOW_BATTERY_SLOWDOWN))
#endif
#if (TREND_WINDOW_MS * TREND_STALE_FACTOR) < SAMPLE_LONGEST_MS
#error "TREND_STALE_FACTOR is too small for the slowest sampling cadence: every trend window would be discarded as stale and the trend would read STEADY for ever"
#endif
#if TREND_WINDOW_MS < 4000UL
#error "TREND_WINDOW_MS is too short to average out ripple"
#endif

#if LEVEL_SAFE_MM <= LEVEL_HIGH_MM
#error "LEVEL_SAFE_MM must be greater than LEVEL_HIGH_MM"
#endif

#if DEBOUNCE_SAMPLES < 1
#error "DEBOUNCE_SAMPLES must be at least 1"
#endif

// Resistive rain plate, Node 1 only. The board reads HIGH when dry and falls
// sharply when wet, so anything BELOW this counts as raining. Run
// tests/06_rain, write down your dry and wet values, and put a number roughly
// midway between them here. It lives in config.h with every other tunable
// rather than buried in sensor_rain.cpp, because it is calibrated per plate.
// ---------------- Rain plate ----------------
// TWO thresholds, not one, because a single comparison chatters. A plate
// sitting near the boundary flips wet and dry every sample, and the gateway now
// uses rainfall per site to decide whether a stall is a blockage, so chatter
// there turns into a verdict that flickers between storm and blockage.
//
// Below RAIN_WET_ADC it is raining. Above RAIN_DRY_ADC it is not. In between it
// keeps whatever it last decided. The board reads HIGH when dry and falls when
// wet, so WET is the LOWER number.
//
// These are the values set on our three plates. A new plate needs its own pair:
// two plates from the same bag can differ by hundreds of counts.
#ifndef RAIN_WET_ADC
#define RAIN_WET_ADC         2500     // set on the bench with tests/06_rain
#endif
#ifndef RAIN_DRY_ADC
#define RAIN_DRY_ADC         3000     // set on the bench with tests/06_rain
#endif
// Whichever polarity the board has, WET is the threshold you cross going into
// rain and DRY is the one you cross coming out. The numbers swap order between
// the two polarities, the meaning does not.

// WHICH WAY ROUND DOES YOUR BOARD READ?
//
// A resistive plate is one half of a voltage divider, and which half decides
// the polarity. With the plate between AO and ground, wetting it drops its
// resistance and AO FALLS, which is the common FC-37 and YL-83 wiring and the
// default here. Build it the other way round, plate between AO and VCC, and AO
// RISES when wet. Some modules ship wired that way from the factory.
//
// Neither is wrong. Getting it backwards in software is, and the symptom is
// nasty: the node reports rain when it is dry and dry when it is raining, so
// the gateway confirms a blockage during every storm and calls a real blockage
// a storm. Both failures are in the direction that makes the system useless
// and neither looks like a wiring fault from the dashboard.
//
// tests/06_rain works this out for you. It measures both states and prints the
// line to paste, including this one.
#ifndef RAIN_ACTIVE_HIGH
#define RAIN_ACTIVE_HIGH     0        // 1 if your plate reads HIGHER when wet
#endif

#if RAIN_ACTIVE_HIGH
  #if RAIN_WET_ADC <= RAIN_DRY_ADC
  #error "With RAIN_ACTIVE_HIGH the board reads HIGH when wet, so RAIN_WET_ADC must be ABOVE RAIN_DRY_ADC"
  #endif
  #if (RAIN_WET_ADC - RAIN_DRY_ADC) < 200
  #error "Hysteresis gap is too small to stop the plate chattering at the boundary"
  #endif
#else
  #if RAIN_DRY_ADC <= RAIN_WET_ADC
  #error "RAIN_DRY_ADC must be above RAIN_WET_ADC: this board reads LOW when wet. If yours reads HIGH when wet, set RAIN_ACTIVE_HIGH to 1"
  #endif
  #if (RAIN_DRY_ADC - RAIN_WET_ADC) < 200
  #error "Hysteresis gap is too small to stop the plate chattering at the boundary"
  #endif
#endif

// SWITCHED PLATE POWER. Set to a free GPIO to power the plate only while it is
// being read. Leave at -1 to keep the plate on 3V3 permanently.
//
// Do this. A resistive plate held at a steady DC voltage electrolyses the water
// bridging its electrodes and the tracks corrode away, which is why these are
// sold as consumables. Powering it for a few milliseconds per sample instead of
// continuously extends that from weeks to a season or more.
//
// It is now a correctness issue as well as a maintenance one. A corroded plate
// reads DRY for ever, and the gateway uses dryness to escalate a stall into a
// blockage, so a dead plate manufactures maintenance callouts during storms.
// gateway/blockage.cpp guards against one bad plate, but not against three.
//
// GPIO13 is free on this pin map and has no strapping function. Wire the plate's
// VCC there instead of 3V3.
#ifndef RAIN_POWER_PIN
#define RAIN_POWER_PIN       -1       // set to 13 once the plate VCC is moved
#endif
#ifndef RAIN_SETTLE_MS
#define RAIN_SETTLE_MS       10       // the divider settles in well under this
#endif

// The ESP32 ADC is noisy enough that a single analogRead scatters by tens of
// counts on a signal that is not moving. The thresholds above sit a quarter of
// the wet-to-dry separation apart, which on a poor plate is only about 100
// counts, so one noisy sample can cross a threshold that the plate itself never
// went near. Averaging kills it for about a millisecond of work.
//
// tests/06_rain averages the SAME number of samples when it calibrates. If the
// two disagree, the thresholds are measured on one signal and applied to a
// different one. tests_host/source_invariants_check.sh fails the build if they drift.
#ifndef RAIN_ADC_SAMPLES
#define RAIN_ADC_SAMPLES     16
#endif
#if RAIN_ADC_SAMPLES < 1
#error "RAIN_ADC_SAMPLES must be at least 1: sensor_rain.cpp divides by it"
#endif
#if RAIN_ADC_SAMPLES > 64
#error "RAIN_ADC_SAMPLES above 64 buys no accuracy and lengthens every rain read"
#endif

// Catch a switched-power pin that lands on something already in use. The build
// otherwise succeeds and the symptom is the OTHER peripheral failing, which is
// a long way from the line that caused it.
#if RAIN_POWER_PIN >= 0
  #if RAIN_POWER_PIN == PIN_LORA_SCK  || RAIN_POWER_PIN == PIN_LORA_MISO || \
      RAIN_POWER_PIN == PIN_LORA_MOSI || RAIN_POWER_PIN == PIN_LORA_NSS  || \
      RAIN_POWER_PIN == PIN_LORA_RESET|| RAIN_POWER_PIN == PIN_LORA_DIO0 || \
      RAIN_POWER_PIN == PIN_US_TRIG   || RAIN_POWER_PIN == PIN_US_ECHO   || \
      RAIN_POWER_PIN == PIN_OLED_SDA  || RAIN_POWER_PIN == PIN_OLED_SCL  || \
      RAIN_POWER_PIN == PIN_RAIN_ANALOG || RAIN_POWER_PIN == PIN_RAIN_PULSE || \
      RAIN_POWER_PIN == PIN_BEACON    || RAIN_POWER_PIN == PIN_BUZZER    || \
      RAIN_POWER_PIN == PIN_VBAT
    #error "RAIN_POWER_PIN collides with a pin already assigned in this pin map"
  #endif
  #if RAIN_POWER_PIN == 34 || RAIN_POWER_PIN == 35 || \
      RAIN_POWER_PIN == 36 || RAIN_POWER_PIN == 39
    #error "RAIN_POWER_PIN is input only on the ESP32 and cannot drive the plate"
  #endif
  // GPIO6 to GPIO11 are the SPI flash the firmware itself is running from.
  // Driving one does not fail gracefully: the board stops booting.
  #if RAIN_POWER_PIN >= 6 && RAIN_POWER_PIN <= 11
    #error "RAIN_POWER_PIN is wired to the SPI flash; the board will not boot"
  #endif
  // Strapping pins are sampled at reset to choose the boot mode. A plate on one
  // of these holds it at whatever the driver leaves it at across a reset, and
  // the symptom is a node that boots correctly on the bench and refuses to boot
  // in the field. GPIO2 is already avoided for the beacon for the same reason.
  #if RAIN_POWER_PIN == 0  || RAIN_POWER_PIN == 2 || \
      RAIN_POWER_PIN == 12 || RAIN_POWER_PIN == 15
    #error "RAIN_POWER_PIN is a boot strapping pin; holding it can stop the board booting"
  #endif
#endif

// Nodes without a local rain sensor receive rain state after an uplink. A
// retained value may still drive sampling and sleep policy, but it must expire
// for the dry-only blockage rule: one missed "rain stopped" downlink must not
// suppress blockage detection forever, and a missing gateway must not create a
// false dry-weather blockage alarm.
#define RAIN_BCAST_TTL_MS    (20UL * 60UL * 1000UL)

// ---------------- Sensor geometry and timing ----------------
// Measured from the TRANSDUCER FACE straight down to the dry floor of the
// drain or the test vessel. Not the pipe length, not the height of whatever
// the sensor is clamped to, and not the height above the ground the vessel is
// standing on. Depth is computed as MOUNT_HEIGHT_MM minus the measured
// distance, so this is the number that reads zero when the drain is empty.
//
// Two constraints bound it, and on a small test rig they bite from opposite
// directions.
//
// Too low and the water enters the dead zone. The module returns nothing
// closer than US_MIN_VALID_MM, so the deepest water the engine acts on has to
// sit at least that far below the face. The #error below enforces it.
//
// Too high and the walls become the target. The JSN-SR04T has a 75 degree
// measuring angle, so the beam spreads to roughly 1.5 times the distance
// below the face. Anything rigid inside that cone and closer than the water
// returns the echo first, and the reading then locks onto the rim or the wall
// and never moves when the level changes. On a bucket 300 mm across the rim
// leaves the cone only once the face sits under about 195 mm above it. A rig
// reading a large fixed number that ignores poured water has this fault, not
// a wiring fault.
//
// 500 mm is the measured mount on the three bench rigs. It clears the 250 mm
// dead zone with 350 mm to spare at LEVEL_SAFE_MM, and on a 367 mm bucket the
// rim leaves the beam cone comfortably. Re-measure at every real install, it is
// a property of the mounting and not of the design.
#ifndef MOUNT_HEIGHT_MM
#define MOUNT_HEIGHT_MM      500      // transducer face to dry floor; set at install
#endif

// The JSN-SR04T needs a LONGER trigger pulse than the HC-SR04. The HC-SR04
// datasheet value of 10 us is widely copied and gives unreliable or missing
// echoes on this sensor; the manufacturer and multiple independent sources
// specify about 20 us for stable operation. This single number is the
// difference between a sensor that works and one that intermittently returns
// nothing.
#define US_TRIG_PULSE_US     20

// Echo timeout. Distance in mm is roughly us * 0.1715, so 30 ms covers about
// 5.1 m, comfortably beyond any sensible mount height for this application.
#define US_ECHO_TIMEOUT_US   30000UL

// Readings below the sensor dead zone are physically impossible and are a
// known failure mode of this module: it intermittently reports a minimum
// distance glitch of roughly 20 to 25 cm while looking at a target metres
// away. Treating those as valid would inject a false high water level, so
// they are discarded before the median filter ever sees them.
#define US_MIN_VALID_MM      250

// Geometry sanity check. Water standing at LEVEL_SAFE_MM is the deepest level
// the rule engine has to measure rather than merely notice, and it sits that
// far above the floor, so the distance from the face down to it is
// MOUNT_HEIGHT_MM - LEVEL_SAFE_MM. If that falls inside the dead zone the
// node stops reporting exactly when the road becomes unsafe, which is the one
// moment it must not. Caught here rather than on the rig.
#if (MOUNT_HEIGHT_MM - LEVEL_SAFE_MM) < US_MIN_VALID_MM
#error "MOUNT_HEIGHT_MM too low: water at LEVEL_SAFE_MM would sit inside the sensor dead zone. Raise the sensor, or lower LEVEL_SAFE_MM."
#endif

// POWER CHAIN ON BATTERY.
// two cells in parallel (3.0 to 4.2 V) -> MT3608 5 V BOOST CONVERTER -> ESP32
// VIN and the ultrasonic sensor. The solar node adds a CN3065 charger between
// the panel and the cells. The boost is required, not optional: the DevKit V1 VIN
// pin is specified for 4.8 V to 12 V and regulates down through an AMS1117-3.3,
// so a single cell never reaches the minimum and the board browns out. The
// same 5 V rail then gives the ultrasonic sensor the true 5 V it prefers.
// Set the boost output to 5.0 V and verify with a multimeter before connecting.

// SENSOR SUPPLY WARNING.
// The JSN-SR04T is specified for 3.0 to 5.5 V, but independent testing reports
// erratic readings when it is run near the bottom of that range, and the usual
// recommendation is to power it from a true 5 V rail. On USB the ESP32 VIN pin
// is 5 V and all is well. On battery, VIN is the cell voltage, 3.0 to 4.2 V,
// which is in spec but in the region where readings are reported to degrade,
// and it falls as the cell discharges. Verify sensor behaviour across the full
// cell voltage range during calibration in Part 9 of the build guide. If the
// readings drift with battery state, fit a small 5 V boost converter on the
// sensor supply. This is the single most likely reason for a node that reads
// correctly on the bench and badly in the field.

// ---------------- Watchdog ----------------
// The timeout must comfortably exceed the longest legitimate blocking period.
// The slowest of those is a sample cycle: up to 7 ultrasonic pings at 30 ms
// spacing plus a transmission and a receive window, well under 5 s. 30 s gives
// a wide margin while still catching a genuine hang quickly.
#define WDT_ENABLED          1

// The timeout must exceed the LONGEST legitimate gap between watchdog feeds,
// which is the slowest sample interval, not the fastest. Hard-coding 30 s here
// was wrong: in the dry state the node samples every 60 s, so a healthy idle
// node reset itself before it ever took a reading, forever. Found on hardware,
// not in simulation.
//
// Derived rather than typed, so it cannot silently fall below the interval
// again if SAMPLE_DRY_MS is retuned. Low battery triples the quiet intervals,
// hence the factor of three, plus a margin for the sample cycle itself.
// Derived from SAMPLE_LONGEST_MS, not from a hand-copied 3. The literal was
// left behind when LOW_BATTERY_SLOWDOWN was hoisted out of power.cpp, so at a
// slowdown of 4 the timeout stayed at 210 s against a real slowest cadence of
// 240 s and every guard still passed. That is the same defect the hoist was
// made to remove, in the two places it mattered most.
#define WDT_TIMEOUT_S        ((SAMPLE_LONGEST_MS / 1000UL) + 30UL)

// ---------------- Low battery ----------------
#define BATT_LOW_PCT         25       // below this, intervals stretch further
#define BATT_POLL_MS          60000UL // avoid hammering the ADC in the fast loop

#endif
