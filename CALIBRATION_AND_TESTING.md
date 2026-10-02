# Calibration and testing

Storm-Drain Network, sixth edition firmware. Every parameter below was read out
of the shipped `config.h` and traced to the code that consumes it, so line
numbers and behaviour match what you are about to flash.

Work top to bottom. Each step depends on the one above it, and calibrating a
threshold before the sensor reads correctly just bakes the sensor error into
the threshold.

---

## The parameters at a glance

Eight numbers need setting per node, plus a polarity flag if your plates are
wired the less common way round. Everything else in `config.h` either ships
correct or is tuned from the range test in week 5.

| Parameter | File, line | Ships as | Decides |
|---|---|---|---|
| `MOUNT_HEIGHT_MM` | node/config.h 550 | 500 | Where zero depth is. Everything else rides on it |
| `LEVEL_SAFE_MM` | node/config.h 241 | 150 | Depth at which the road stops being passable |
| `LEVEL_HIGH_MM` | node/config.h 242 | 100 | Depth that raises attention and arms the recession timer |
| `RATE_WARN_MM_MIN` | node/config.h 243 | 20 | Rise rate that raises attention on its own |
| `RECEDE_WINDOW_MS` | node/config.h 260 | 20 min field, 2 min bench | How long water may sit before the node calls it stalled |
| `RECEDE_DROP_MM` | node/config.h 286 | 30 | How much fall counts as the drain working |
| `RAIN_WET_ADC` | node/config.h 531 | 2500 | Crossed going into rain |
| `RAIN_DRY_ADC` | node/config.h 534 | 3000 | Crossed coming out of it |
| `RAIN_ACTIVE_HIGH` | node/config.h 415 | 0 | 1 if your plate reads HIGHER when wet |

The last two are per plate, not per design. Two plates out of the same bag
differ by hundreds of counts, so Step 5 is run once for each of the three and
each node gets its own pair.

One more is hardware rather than a threshold. `DIVIDER` in `node/power.cpp`
line 10 ships as 2.0 and has to match the resistor pair you actually soldered.

### A naming trap, read this before you touch either threshold

`LEVEL_SAFE_MM` is larger than `LEVEL_HIGH_MM`. That looks backwards and it is
not. `LEVEL_SAFE_MM` means the limit of safe depth, the point at which the road
becomes unsafe, so it sits above the attention threshold. `config.h` enforces
the ordering with an `#error`, so getting it the wrong way round fails the
build rather than shipping a node that never reports waterlogging.

### What the node actually publishes, which is two fields and not one

A node reports a SEVERITY and a TREND, independently, packed into one byte.
Reading `status_engine.cpp` straight out:

    severity, from depth alone
      level >= LEVEL_SAFE_MM     -> WATERLOGGED
      level >= LEVEL_HIGH_MM     -> ELEVATED
      otherwise                  -> NORMAL

    trend, from movement over TREND_WINDOW_MS
      rose  by TREND_DEADBAND_MM or more   -> RISING
      fell  by TREND_DEADBAND_MM or more   -> RECEDING
      neither                              -> STEADY
      high and not receding for
      RECEDE_WINDOW_MS                     -> BLOCKED

Severity is depth and nothing else. `RATE_WARN_MM_MIN` no longer raises it: a
fast rise in shallow water is a prediction, not a measurement, so it moves the
trend and the sampling cadence rather than lighting a road beacon over water
nobody could trip in.

The deadband is a RATE, not a distance. It is `TREND_DEADBAND_MM` per
`TREND_WINDOW_MS`, and the engine scales it by the time actually elapsed, so
8 mm across one minute and 24 mm across three minutes are the same verdict. That
matters because the sampling interval changes with state and triples on a low
battery, and without the scaling the trend field would be three times more
sensitive in the quietest state on the weakest battery.

### BLOCKED on the node is not the same as a blockage verdict

A node can measure that water is not going down. It cannot measure why. A
blocked drain and a drain being out-rained look identical from one level
reading. What separates them is rainfall at that site and what the other drains
are doing at the same moment, and no single node can see the second.

So the node reports BLOCKED on level alone, takes no rain input into that rule
at all, and reports its own plate as a separate field. The GATEWAY turns STALLED
into a verdict:

    none        the node is not stalled
    storm       stalled, raining, and every reporting node is stalled together
    suspected   stalled during rain while a neighbour is still draining, or
                stalled with nothing available to corroborate the dryness
    confirmed   stalled with no rainfall to explain it, corroborated

That is why calibrating the plates matters more than it looks. A plate that
reads dry for ever, which is what a corroded one does, turns every storm into a
maintenance callout to a named drain.

---

## Step 0. The mount, before any number goes in the file

This is where a calibration day gets lost. Get it right and the rest takes an
hour.

### Measure from the transducer face

`MOUNT_HEIGHT_MM` is the vertical distance from the flat face of the
transducer straight down to the inside bottom of the vessel. Not the length of
the pipe. Not the height of the bench. Not the height of whatever the sensor is
cable-tied to. `sensor_level.cpp` computes

    depth = MOUNT_HEIGHT_MM - measured_distance

so this is the number that must read zero when the vessel is dry.

### Keep the beam clear

The JSN-SR04T has a 75 degree measuring angle. The beam spreads to roughly 1.5
times the distance below the face, so at 300 mm down it is already 460 mm
across. Anything rigid inside that cone and closer to the sensor than the water
returns the echo first, and the reading then locks onto that object and stops
responding to water entirely.

Edges are the problem. A bucket rim, a bracket, a clamp, a cable tie, the cut
end of the pipe. These scatter sound straight back. A smooth vertical wall is
usually fine because the beam hits it at a grazing angle and reflects away
rather than returning, which is why a sensor works in a tank at all.

The clearance rule, for a round vessel of radius R, is that the transducer face
must sit less than 1.3 times R above the rim.

| Vessel across | Radius | Face may sit above the rim by |
|---|---|---|
| 250 mm | 125 mm | under 160 mm |
| 300 mm | 150 mm | under 195 mm |
| 400 mm | 200 mm | under 260 mm |

If the transducer is tied inside the pipe rather than hanging below its mouth,
the pipe wall sits in the cone and wins. The face needs to be at or below the
bottom edge of the pipe with clear air around it.

### Stay out of the dead zone

The module returns nothing closer than 250 mm, which `US_MIN_VALID_MM` enforces
by discarding anything nearer. So the deepest water you want to measure has to
sit at least 250 mm below the face.

    maximum measurable depth = MOUNT_HEIGHT_MM - 250

`config.h` refuses to build if `MOUNT_HEIGHT_MM - LEVEL_SAFE_MM` falls below
250, because that would mean the node goes blind at exactly the depth that
makes the road unsafe. With the shipped thresholds the floor is 400 mm.

### Where that leaves a bench vessel

Both constraints together, for a bucket about 300 mm across and 350 mm tall
holding water up to 150 mm deep.

    floor from the dead zone      400 mm above the vessel bottom
    ceiling from the beam cone    545 mm above the vessel bottom
    use                           400 to 450 mm

At 450 mm you measure 0 to 200 mm of water, the rim sits 100 mm above the face
and well outside the cone, and the shipped thresholds of 100 and 150 work
without changing them. That is the setting to aim for.

---

## Step 1. Set `MOUNT_HEIGHT_MM` and prove the sensor tracks water

Set `BRINGUP_MODE` to 1 in `node/config.h` first. It samples every 2 seconds,
reports every 10, disables deep sleep so the USB serial link stays up, and
prints every sample. Turn it off again before you record any timing numbers.

Measure the face to the dry bottom with a tape, write that number into
`MOUNT_HEIGHT_MM`, and flash. The serial line you are watching is

    sample: level=0mm rate=0 rain=dry  NORMAL

### The two minute check

Empty vessel. Depth should read within about 10 mm of zero. A large fixed
number that never moves means the beam is on the rim or the pipe, not the
bottom. Lower the sensor 50 mm and try again.

Mark 100 mm inside the vessel, pour to the mark, wait for three samples. Depth
should read about 100 mm and hold steady. If it does not move at all, the beam
is still locked onto something rigid.

### Then take the calibration table

This is the number that goes in your report, so do it properly. Pour in
measured steps, let three samples settle at each, and record what the node
says against what the ruler says.

| Water by ruler | Node reports | Error |
|---|---|---|
| 0 mm | | |
| 25 mm | | |
| 50 mm | | |
| 75 mm | | |
| 100 mm | | |
| 125 mm | | |
| 150 mm | | |
| 175 mm | | |
| 200 mm | | |

Then drain back down through the same steps and record again. Ultrasonic
readings sometimes differ filling versus draining because of surface ripple, and
the difference is worth knowing.

Accept the mount if errors stay within about 10 mm and no step is wildly out. A
constant offset across every row means `MOUNT_HEIGHT_MM` is off by that amount,
so adjust it and rerun rather than accepting a biased sensor. Errors that grow
with depth mean the beam is catching a wall as the surface rises.

### If the reading is noisy rather than wrong

`US_SPREAD_MM` is 15. When the spread across pings exceeds it the filter keeps
pinging up to `US_MEDIAN_MAX` of 7, so a noisy surface costs time rather than
accuracy. `level_last_pings()` tells you how many it needed. Consistently
hitting 7 means ripple or foam, so let the surface settle before reading, or
float a piece of foam board to calm it.

---

## Step 2. Set the depth thresholds

With the sensor honest, the thresholds are a judgement call about the site, not
a measurement.

### `LEVEL_SAFE_MM`, the road unsafe depth

The depth at which you would say the road is no longer passable. For a real
Dhaka street, ankle deep is a reasonable line, so 150 mm as shipped is
defensible and easy to justify in a viva.

This one also drives the beacon through `engine_road_unsafe()`, which reads the
raw level rather than the published state, on purpose. The debounce may still be
publishing NORMAL while the water is over the road right now, and a safety light
should not wait three samples.

### `LEVEL_HIGH_MM`, the attention depth

Lower than the safe depth, and it does two jobs. It is the depth at which
severity becomes ELEVATED, and it is the gate that arms the recession timer. Set
it where you want the network to start paying attention, around two thirds of
the safe depth. 100 mm as shipped.

### Verify both on the rig

Pour slowly past each threshold and watch the first word of the state.

    below 100 mm            NORMAL
    100 to 149 mm           ELEVATED
    150 mm and above        WATERLOG

Remember the debounce. `DEBOUNCE_SAMPLES` is 3, so a state has to hold for
three consecutive samples before it publishes. In bringup that is about 6
seconds. In production at `SAMPLE_EVENT_MS` of 5 seconds it is about 15. That
delay is deliberate and it is what keeps a single bad ping from raising an
alarm.

---

## Step 3. Set `RATE_WARN_MM_MIN`

The rise rate in millimetres per minute that raises attention on its own,
regardless of depth. It exists so a drain filling fast from nothing still gets
watched closely before the water is deep.

Note what it no longer does. It does not raise SEVERITY. A fast rise in shallow
water is a prediction rather than a measurement, so it drives the sampling and
reporting cadence and the local display, and it does not light a road beacon
over water nobody could trip in. Severity is depth alone.

`level_rate_mm_min()` computes it from the real elapsed time between the last
two samples, and returns 0 if the gap exceeds 30 minutes, because dividing a
real change by a long sleep produces a number that says nothing about how fast
the water is moving now.

To set it, pour at a rate you would call alarming and read the rate field off
the serial line. Set the threshold somewhat below what you measured. 20 mm per
minute as shipped is a sensible starting point.

One known limit worth writing into your report. The rate travels in the packet
as a signed byte, so it saturates at 127 mm per minute. In the August capture 3
of 91 readings hit that ceiling. It does not affect the rule, since anything
over 127 is far past any sane threshold, but the archived number is clipped and
you should say so rather than have someone find it.

---

## Step 4. Set the blockage rule, `RECEDE_WINDOW_MS` and `RECEDE_DROP_MM`

This is the part that makes the project novel, so it deserves the most care.

### What the rule actually does

Straight from `status_engine.cpp`. The window opens only when rain has stopped
and the level is at or above `LEVEL_HIGH_MM`. While it is open the engine tracks
the lowest level seen. If the water falls by `RECEDE_DROP_MM` or more from the
level the window started at, the drain is working, so the window re-anchors
immediately at the current level and time. If the window instead reaches
`RECEDE_WINDOW_MS` without that much fall, the state becomes BLOCKAGE
SUSPECTED.

The re-anchor matters and it is easy to miss. Without it, a drain that emptied
properly and then filled again from a second burst could be retroactively
declared blocked. Re-anchoring means a proven drop can never be erased by a
later rise.

### Setting the two numbers

`RECEDE_DROP_MM` is the fall that counts as proof the drain is working. It has
to be comfortably larger than the sensor noise or a still pond will look like
it is draining. With sensor error around 10 mm, 30 mm as shipped gives three
times the margin.

`RECEDE_WINDOW_MS` is how long you allow water to sit before calling it. Too
short and every slow drain is a false alarm. Too long and the alert arrives
after the flood. Twenty minutes as shipped suits a real street drain.

### The bench window is automatic now, and that is one less thing to revert

Twenty minutes per trial makes a detection rate unaffordable. Ten stalled
trials and ten draining ones is over seven hours of watching a bucket.

`BRINGUP_MODE` scales the recession WINDOW and nothing else:

    bringup      RECEDE_WINDOW_MS  2 min     RECEDE_DROP_MM  30
    production    RECEDE_WINDOW_MS  20 min    RECEDE_DROP_MM  30

THE DROP DOES NOT SCALE, and this table used to say it did. Bringup briefly
carried a 15 mm drop and that was wrong. The window is a CLOCK and the drop is
a DISTANCE. At 15 mm the threshold sat at 1.5 times sensor error against a 3
times margin, and below `2 x TREND_DEADBAND_MM`, so still water could clear it.

Everything that is a clock scales with bringup. Nothing that is a distance
does. The rule itself is untouched either way, which is the contract bringup
mode already had for the sampling and reporting intervals. A trial now takes
about two and a half minutes including the debounce, so twenty of them is an
hour rather than an afternoon.

This used to be a manual edit with an instruction to put it back afterwards.
That is a step in the middle of a long bench session, and forgetting the second
half means every figure after it is measured against a two minute window while
the report claims twenty. Now reverting it is the same single switch as
everything else.

The gateway's `RAIN_MEMORY_MS` scales with it, 30 minutes down to 3. At the
field value against a two minute bench window, a plate wetted once would keep
its site counted as raining for fifteen consecutive trials, and the dry case
needed to confirm a blockage could never be reached at all.

---

## Step 5. Calibrate the rain plate. Once per plate, all three nodes

Every node carries its own plate now. Rain is local, and a downpour over the
intersection is not evidence about an outfall half a kilometre away, so each
site is judged against its own reading.

Two numbers, not one, and they belong to the plate rather than to the design.

### Wire it first, and switch the power

    plate GND  ->  GND
    plate AO   ->  GPIO32
    plate VCC  ->  GPIO13,  not 3V3

The last one is the part people skip. A resistive plate held at a steady DC
voltage electrolyses the water bridging its electrodes and the tracks corrode
away, which is why these are sold as consumables. Powering it for ten
milliseconds per reading instead of continuously takes that from weeks to a
season.

It is a correctness issue as well as a maintenance one. A corroded plate reads
DRY for ever, and the gateway uses dryness to escalate a stall into a blockage,
so a dead plate manufactures a maintenance callout during every storm.

Ignore the blue trimmer pot on the board. It only sets the digital D0 output,
which this firmware does not use. The threshold is set in software.

### Run the calibration sketch

`tests/06_rain` walks you through it. Three steps, about a minute.

    1  plate completely dry. It waits for the reading to settle by itself
       rather than trusting a number you read off mid-transition
    2  wet the plate, and you get 20 seconds to do it
    3  it measures the wet reading the same way

Wet it the way rain would: sprinkle water across it. Do not dunk it and do not
use a finger. A finger is far more conductive than rain and gives a wet value
you will never see outdoors, which sets the threshold too low and leaves the
node reading dry in a drizzle.

It prints the two `#define` lines to paste into `node/config.h`, and it also
reports how much the plate wanders on its own. If that noise is wider than the
gap between the two thresholds it says so, because then noise alone can flip
the reading. A long unshielded AO wire is the usual cause.

    dry reading      ______
    wet reading      ______
    separation       ______   under 400 counts, use a different plate
    polarity         ______   does it read higher or lower when wet
    RAIN_WET_ADC     ______   crossed going into rain
    RAIN_DRY_ADC     ______   crossed coming out of it
    RAIN_ACTIVE_HIGH ______   1 only if the plate reads HIGHER when wet

Between the two the node holds whatever it last decided.

### Polarity, which the sketch works out for you

A resistive plate is one half of a voltage divider and which half decides which
way the reading moves. Plate between AO and ground, wetting it drops its
resistance and AO falls. Plate between AO and VCC and AO rises instead. Both
wirings ship on real modules, so the sketch measures which one you have rather
than trusting a datasheet picture, and prints `RAIN_ACTIVE_HIGH` when you need
it.

Getting this backwards in software is the expensive mistake. The node then
reports rain when it is dry and dry when it is raining, so the gateway confirms
a blockage during every storm and dismisses a real blockage as weather. Both
failures make the system useless and neither looks like a wiring fault from the
dashboard.

`config.h` rejects a threshold pair ordered against the polarity you declared,
and the message names `RAIN_ACTIVE_HIGH` so the fix is obvious rather than a
puzzle. The one thing it cannot catch is measuring dry and wet the wrong way
round, which looks identical to an inverted board. If the sketch tells you the
plate reads higher when wet, check that step 1 really was the dry reading before
you accept it.

### If the separation is thin

A healthy plate swings well over a thousand counts between bone dry and properly
wet. The sketch accepts 400 and warns below 800, because it is your plate and
your call, but a thin swing usually means one of three things. The plate was not
wetted enough, and a sprinkle across the whole surface is what you want rather
than a drop in one corner. The plate is already corroded, which is what switched
power exists to slow down. Or both readings sit above about 3000 counts, where
the ESP32 ADC is compressed and non-linear, so a given voltage change buys fewer
counts than it would lower down. A larger series resistor moves the whole range
down and spreads it out. That gap is what stops
the plate chattering when it sits near the boundary, and chatter matters
because the gateway now decides blockage against rainfall per site, so a
flickering plate makes the verdict flicker with it.

`config.h` rejects a pair the wrong way round and a gap under 200 counts with an
`#error`, so a mistake here fails the build rather than shipping.

### Label the plates

Write the node number on each one. They are not interchangeable and the
thresholds are not transferable.

### Then verify against the real firmware

Flash the node and watch the `rain=` field on the sample line flip 0 to 1 as you
wet and dry the plate. The calibration sketch and the firmware average the same
number of ADC samples deliberately, so what you calibrated is what the node
thresholds. `tests_host/source_invariants_check.sh` fails the build if those two
ever drift apart.

---

## Step 6. Calibrate the battery divider

### Wire it before you calibrate anything else, even if you do not care about battery

An unwired GPIO35 floats near zero. `read_battery_pct()` computes
`(0 - 3.2) / 1.0 * 100` and clamps it, so the node reports 0 percent and
`lowPower` is true from the first boot. `power.cpp` then multiplies the DRY
sampling interval by `LOW_BATTERY_SLOWDOWN`, so a bringup node samples every
6 seconds instead of 2 while the vessel is empty, and a field node every 180
seconds instead of 60.

It only stretches the QUIET states, so the moment there is water in the vessel
the cadence is normal again. That is exactly what makes it confusing: the node
looks sluggish while you are setting up and then behaves correctly once you
start pouring, which reads as an intermittent fault rather than a missing
resistor pair.

Watch for `batt=0%` on the sample line. That is the symptom.


`DIVIDER` in `node/power.cpp` line 10 ships as 2.0, which assumes two equal
resistors. It has to match what you actually built.

    DIVIDER = (R_top + R_bottom) / R_bottom

Measure the real cell voltage with a multimeter, compare against what the node
reports, and adjust `DIVIDER` until they agree.

Until the divider is wired, `PIN_VBAT` on GPIO35 floats and the battery
percentage is noise. That is the state the last capture was in, so do this
before you log anything that includes a battery column.

The code uses `analogReadMilliVolts()`, which applies the per-chip calibration
burned into eFuse, and averages eight reads. Do not replace it with the naive
raw over 4095 times 3.3 conversion. The ESP32 ADC is non-linear, ignores roughly
the first 0.21 V, saturates near 3.1 V, and its internal reference varies from
about 1.0 to 1.2 V between individual chips.

---

## Step 7. Prove the blockage rule with numbers

Three figures belong in your report. A detection rate, a false alarm count, and
a latency. Anything less than a measured number is an assertion.

### The two trial types

A stalled trial. Fill above `LEVEL_HIGH_MM` and leave the outlet plugged. The
node should report BLOCKED after the recession window plus the debounce.
The rain plate is irrelevant to this: the node takes no rain input into that
rule, so you can run it wet or dry and the node behaves the same.

A draining trial. Same setup, but open the outlet so the water falls by more
than `RECEDE_DROP_MM` within the window. The node should show CLEARING and
never reach BLOCKED.

Run at least ten of each. Vary the draining trials, some fast, some only just
fast enough, because a rule that only passes on an obviously working drain has
not been tested.

### And a third type, which is the one the network exists for

The gateway decides whether a stall is a blockage or a storm, so that decision
needs its own trials and they need more than one node.

    all three wet, one stalled    -> suspected. Rain falls on all of them, so
                                     the difference is local
    all three wet, all stalled    -> storm. Not a fault
    stalled node dry              -> CONFIRMED, if a second source agrees:
                                     a working forecast, or another node that
                                     also reads dry

Record the gateway's reason string for each, which it prints on the serial line
and writes into the archive. A verdict without its reason is not evidence.

Worth running deliberately: wet two plates, leave the third dry, and stall the
dry one. The gateway treats two unanimously wet neighbours as outvoting one
dissenting dry plate, because a plate that has corroded open reads dry for ever
and would otherwise raise a callout during every storm. One disagreeing
neighbour is not enough to outvote it, because that is the local variation the
per-site plates exist to respect.

### The log sheet

| Trial | Type | Start depth | Outlet plugged at | STALLED at | Latency | Gateway verdict | Correct |
|---|---|---|---|---|---|---|---|
| 1 | stalled | | | | | | |
| 2 | draining | | | | | | |
| 3 | | | | | | | |

### The three figures

    detection rate  = stalled trials that reported BLOCKED / stalled trials
    false alarms    = draining trials that reported BLOCKED
    latency         = time from the outlet being plugged to STALLED published

Expect the latency to land slightly above `RECEDE_WINDOW_MS`, because the
debounce adds up to three sample intervals on top. Say so when you report it.
The window plus the debounce is the honest number and it is a design choice, not
an error.

The debounce is asymmetric and that is deliberate, so measure both directions.
Raising a stall takes three agreeing samples because it ends in a callout.
Clearing one is immediate, because what clears it is direct evidence against it:
the water has dropped below `LEVEL_HIGH_MM`, or the short window can see it
going down. Holding a stall on a drain that is visibly draining is the wrong
direction to be slow in.

### Also worth testing

Pull the sensor connector mid-trial. The node should report no reliable
ultrasonic sample and hold its last good reading rather than dropping to zero.
Zero reads as a dry drain, which is the dangerous direction, and the firmware
avoids it on purpose.

Power cycle mid-window. The recession timer lives in RTC memory on the absolute
clock, so it survives deep sleep but a true power cycle clears it. Know which
one you did before you interpret the result.

---

## Moving from the bucket to a real drain

The thresholds are physical depths, so most of them carry over unchanged. Only
two numbers need revisiting.

`MOUNT_HEIGHT_MM` obviously, measured again at the new mount.

`RECEDE_WINDOW_MS` back to 20 minutes, or to whatever the real drain's emptying
time justifies. A bench vessel empties in seconds and a street drain does not,
so the bench value proves the rule works and tells you nothing about the right
window for the street.

Check the beam clearance again at the new mount. A drain throat is narrower than
a bucket and the clearance rule is unforgiving about that.

---

## When it will not behave

| What you see | What it is |
|---|---|
| Depth is a large fixed number, ignores water | Beam is on the rim, the bracket or the pipe edge. Lower the sensor |
| Depth reads 0 and never changes | No valid echo ever returned. Check the 5 V rail, then the trigger and echo wiring, then the divider on the echo line |
| Depth jumps around by tens of mm | Ripple or foam. Let it settle, or check `level_last_pings()` for the filter hitting 7 |
| Every reading is out by the same amount | `MOUNT_HEIGHT_MM` is wrong by that amount |
| Error grows as the water rises | Beam catching a wall as the surface comes up |
| Severity never leaves NORMAL | Thresholds above anything you are producing, or the sensor has never returned a valid echo, in which case the engine deliberately does not run |
| STALLED never appears | Level never reaches `LEVEL_HIGH_MM`, or the water is falling by `RECEDE_DROP_MM` inside the window and restarting the timer the timer, or the window is longer than your patience |
| STALLED appears and will not clear | The level is still above `LEVEL_HIGH_MM`. Clearing is immediate once it drops, so if it persists the water really is still there |
| Trend reads STEADY through an obvious rise | The sampling interval has outrun `TREND_WINDOW_MS * TREND_STALE_FACTOR`, which reports STEADY on purpose rather than inventing a verdict from two readings too far apart. Check the battery: a low one triples the dry cadence |
| Gateway says CONFIRMED during a storm | A plate is reading dry when it is not. Recalibrate it, and check it is not corroded. Switched power on GPIO13 is what prevents that |
| Gateway says storm when only one drain is blocked | More than one node is stalled at once, which is what storm means. Check whether the others really are holding water |
| Gateway says suspected and never CONFIRMED | Nothing corroborates the dryness. Either the forecast fetch is failing, which the serial line now says explicitly, or no second node is reporting |
| Rain field flickers 0 and 1 | The hysteresis gap is narrower than the plate's own noise. `tests/06_rain` reports that noise, so re-run it and widen the two thresholds |
| Battery percentage is nonsense | Divider not wired, so GPIO35 is floating |

---

## Before you call it calibrated

Set `BRINGUP_MODE` back to 0 on both boards. Leaving it on gives you 2 second
sampling and 10 second reporting, which flattens the power budget and makes
every timing figure in your report wrong.

Confirm `RECEDE_WINDOW_MS` is back to its field value.

Write every number you set into the log, with the date and which node. Node 2
and node 3 need their own `MOUNT_HEIGHT_MM` because they will be mounted
differently, and copying node 1's value across is the mistake waiting to happen.

Calibrate all three rain plates, label them, and write down which pair of
thresholds belongs to which node. They are not interchangeable.

Run `bash tests_host/compile_check.sh` after editing `config.h`. It takes about
ten seconds and it catches a threshold that breaks one of the build guards
before you find out at the rig. Several of those guards exist because of
mistakes already made on this project: a sensor mounted inside its own dead
zone, rain thresholds the wrong way round, a hysteresis gap too narrow to stop
chatter, and a plate power pin landing on a peripheral that was already there.


---

# Testing blockage detection: read this first

This section used to open with a warning that has been designed away, and the
history is worth keeping because it explains the shape of the current firmware.

Nodes 2 and 3 had no rain plate. They took rain state from a gateway broadcast,
and while that was missing or stale the engine was conservatively told it might
be raining, which closed the blockage window completely. So a node 2 or node 3
on the bench with no gateway running could not reach blockage no matter how long
you waited. That cost half an hour on 19 September, and in the field it meant
two of the three nodes had their detection silently disabled whenever the
gateway was off air.

Two changes removed it. Every node now carries its own plate, and more
importantly the node's rule no longer takes ANY rain input. A node measures that
water is not going down and says so. The gateway decides what that means.

So any node, on its own, with no gateway and no network, will reach STALLED.
That is the whole point: the measurement does not depend on anything arriving.

## What the fields mean during a trial

    sample: level=193mm rate=0 rain=dry  WATERLOG BLOCKED

The word after `rain` is the state, one depth word plus one motion word.
`rain` is this node's own plate, `wet` or `dry`. It does not affect the state
word at all, and that is deliberate.

A stalled trial sits with no motion word and steps to BLOCKED once the recession window
expires and three samples agree. A draining trial shows CLEARING and never
reaches 3. That distinction is the result your report is after, and it is
visible sample by sample rather than only at the end.

Watch both fields, not one. The severity and the trend are independent, which is
why a drain can report waterlogged and receding at the same time. Under the old
single-state model reaching BLOCKAGE overwrote WATERLOGGING and the fact that
the road was under water was simply lost.

## The gateway line, which is the other half

    node 3  blockage = confirmed  (stalled and this site is not raining)

The reason string is not decoration. It is what makes the verdict defensible
instead of magic, and it goes into the archive alongside the reading that
produced it. Four verdicts:

    none        not stalled
    storm       stalled, raining, every reporting node stalled together
    suspected   stalled in the rain while a neighbour drains, or stalled with
                nothing available to corroborate the dryness
    confirmed   stalled, this site is dry, and something else agrees it is dry

The gateway also now says what state the forecast is in, because "the forecast
says dry" and "the forecast has not worked since Tuesday" produce the same
verdict and used to look identical on the monitor:

    forecast: dry, 0.0 mm/h, 12 min old
    forecast: none usable, 4 consecutive failures
