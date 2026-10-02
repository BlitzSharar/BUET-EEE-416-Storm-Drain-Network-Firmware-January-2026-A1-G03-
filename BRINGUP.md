# Bringup mode

A one line switch that turns a fifteen minute wait into a ten second one.

## Turn it on

`node/config.h`, line 17, and `gateway/config.h`, near the top:

    #define BRINGUP_MODE       1

Flash both boards. That is the only change.

## What changes

| | Normal | Bringup |
|---|---|---|
| Sampling | 60 s when dry | 2 s always |
| Reporting | 15 min when dry | 10 s always |
| Deep sleep | on | off |
| Sustained backoff | after 30 min | never |
| Gateway offline alarm | 20 min | 2 min |
| Recession window | 20 min | 2 min |
| Gateway rain memory | 30 min | 3 min |
| Forecast refresh | 30 min | 3 min |
| Node serial | quiet | a line every sample |
| Gateway serial | quiet | a heartbeat every 5 s |

**The decision logic is untouched.** Every row above is a clock. The depth
thresholds, `RECEDE_DROP_MM` and the trend deadband are distances and they do
not scale, because a millimetre means the same thing in both modes.

That has one consequence worth knowing before you quote a number. Compressing
time by ten while holding the distances fixed makes the bench recession rule ten
times more lenient in mm per minute than the field rule. The bench proves the
rule fires when water is held and does not fire when it drains. It does not
measure the field's rate threshold, which comes from the real drain's emptying
time.

## What you should see

On the node, within two seconds of boot:

    =====================================================
      BRING-UP MODE, node 1.  Sampling every 2 s,
      reporting every 10 s, deep sleep disabled.
    =====================================================
    sample: level=0mm rate=0 rain=dry  NORMAL
    sample: level=0mm rate=0 rain=dry  NORMAL   -> TRANSMITTING
    LoRa TX complete

On the gateway, every five seconds:

    listening on 433000000 Hz SF8 sync 0x37, packets so far: 0

and on each received packet:

    N1 level=0mm rate=0 status=0 batt=87%

## Reading the result

| What you see | What it means |
|---|---|
| Node prints samples, gateway count rises | The link works. Turn bringup off. |
| Node prints `LoRa TX complete`, gateway count stays 0 | The node completed its local send, so the fault is on the link or at the gateway. Check the gateway is running the integrated receiver and not a mismatched test sketch, then its antenna, module band and RF path. Both boards must show the same frequency, SF and sync word. |
| Node prints `TRANSMITTING` but never `LoRa TX complete` | The node radio did not finish the send. Check its 3.3 V rail, antenna and SPI wiring. |
| Node prints samples but neither TX line | The node runs and the radio never came up. Antenna, then NSS, RST, DIO0, then whether MISO and MOSI are swapped. |
| Node prints nothing at all | The node is not running. Wrong port, or it hung before the banner. |
| Node prints `LoRa init failed` | Its radio did not start. Check the eight wires, then try the spare module. |
| Gateway prints no heartbeat | The gateway hung, or `lora_gw_init()` is looping. It prints `LoRa init failed` and stops if the radio is absent. |

The heartbeat exists for one reason: a gateway receiving nothing looks exactly
like a gateway that has hung, and that ambiguity costs hours on a bench.

## A note on the watchdog

Bringup mode samples every 2 s, which is far inside any watchdog timeout, so
it **hides** watchdog sizing problems rather than exposing them. The first
hardware test of this project found exactly such a problem: the timeout was
30 s while the dry sample interval was 60 s, so a healthy idle node reset
itself before it ever took a reading.

That is fixed, and the timeout is now derived from the sample interval rather
than typed in, so it cannot silently fall below it again. But it is worth
knowing that a clean bringup run does not prove the production timings are
sound. Watch the node for a few minutes with the flag back at 0 before you
trust it.

## Turn it off

Set both flags back to `0` before calibration.

With bringup on the node samples faster than a drain can empty, so every
recession measurement is meaningless, and at roughly 60 mA continuous it
flattens a pair of cells in about two days. The flag ships as `0` for exactly
that reason.

If you run the host test suite with the flag on, `logic_test` skips its cadence
block and says so. That is expected: those assertions check the production
table, which bringup deliberately overrides.

---

# Network setup

`WIFI_MODE` in `gateway/config.h` picks one of three.

| Mode | What it does | Use it when |
|---|---|---|
| 0 | Station only. Joins your hotspot. | Rarely. Mode 2 does this and more. |
| 1 | Access point only. The gateway is the network. | No hotspot available, or you want zero external dependency. |
| **2** | **Both at once.** | **Default. Use this.** |

## Why mode 2

The access point is always there at `192.168.4.1`, whatever the hotspot is
doing, so the laptop can always reach the archive. The station side gives
internet whenever the hotspot is up, so the forecast and the dashboard work.

If the hotspot drops during a demonstration you lose the dashboard and nothing
else. The gateway keeps receiving, keeps archiving, and the laptop can still
download the file.

## Addresses

Three ways in, and they all serve the same page.

    http://stormdrain.local/     works on either network, does not change
    http://192.168.4.1/          the access point, fixed
    http://<station ip>/         printed at boot, changes each session

The `.local` name is mDNS. macOS, iOS and Linux resolve it out of the box.
Windows needs Bonjour, which is usually already installed, and Chrome resolves
it regardless. If it does not work, use the numeric address.

## What was tuned

**Modem sleep is off.** The ESP32 station interface sleeps between beacons by
default, which adds a few hundred milliseconds to every web request and makes
the archive page feel broken. The gateway runs on USB, so there is nothing to
save by leaving it on.

**Nothing blocks at boot.** Neither WiFi nor Blynk is waited on in `setup()`.
A hotspot that is slow to come up used to delay every boot by eight seconds,
and a hotspot that never came up would have stopped the gateway reaching
`loop()` at all. Both connect in the background instead.

**Failure messages are rate limited.** A station that cannot associate cycles
between states every couple of seconds. Printing on every transition buried the
packet lines that actually matter.

**The gateway announces itself as `stormdrain`** rather than `esp32-1a2b3c`, so
it is findable in a router's device list.


---

# Reading the sample line

    sample: level=193mm rate=0 rain=dry  WATERLOG BLOCKED

`sev` is the severity, from depth alone. 0 normal, 1 elevated, 2 waterlogged.

`trend` is the direction, over `TREND_WINDOW_MS`. 0 steady, 1 rising,
2 receding, 3 stalled.

`state` is the two combined into one word, the same twelve readouts the OLED
and the dashboard use, shortened to fit sixteen characters.

`rain` is this node's own plate. It does NOT affect the trend, and that is the
single most important thing to know while testing on the bench.

## Which means any node can reach BLOCKED on its own

The node rule takes no rain input at all. Fill the vessel above
`LEVEL_HIGH_MM`, leave it, and the motion word becomes BLOCKED once `RECEDE_WINDOW_MS`
expires and three samples agree. No gateway, no network, no plate needed.

That was not always true, and the history is worth knowing because it is the
reason the firmware is shaped this way. Nodes 2 and 3 had no plate and took
rainfall from a gateway broadcast. With no broadcast the engine was
conservatively told it might be raining, and the blockage window could not open
at all. The serial line now says nothing of the sort because the situation no
longer exists.

On 19 September a node sat in WATERLOGGING for 28 minutes without ever reaching
BLOCKAGE while the serial line said `rain=0` the whole time, because the print
showed the raw flag and the engine was using the assumed one. Replaying the
recorded levels settled it: with the rain input false the rule fires at 22.0
minutes, with it true it never does. Two of three nodes had their detection
disabled in the field and nothing said so.

## The gateway line is the other half

    node 3  blockage = confirmed  (stalled and this site is not raining)

The node says stalled. The gateway says what that means, using every node's
plate, the forecast and the pattern across all three sites. Four verdicts:
`none`, `storm`, `suspected`, `confirmed`. The reason string in brackets is what
makes it defensible rather than magic, and it is written into the archive beside
the reading that produced it.

The gateway also reports the state of the forecast, because "the forecast says
dry" and "the forecast has not worked since Tuesday" produce the same verdict
and used to look identical:

    forecast: dry, 0.0 mm/h, 12 min old
    forecast: none usable, 4 consecutive failures

A dry forecast is what turns a stall into a CONFIRMED blockage, so knowing which
of those two you are looking at matters.

## Calibrating the rain plate

Run `tests/06_rain` once per plate, before anything else uses the rain field. It
is a guided three step procedure, not a probe: it waits for each reading to
settle, refuses a plate whose wet and dry readings are closer than 400 counts,
reports how much the plate wanders on its own, and prints the two `#define`
lines to paste. Step 5 of `CALIBRATION_AND_TESTING.md` has the detail.

Two numbers, not one, and they belong to the plate rather than the design. Two
plates out of the same bag differ by hundreds of counts, so label them.


---

# The buzzer

## What you should hear

    NORMAL        silence
    ELEVATED      beep ... beep ... beep, once a second
    WATERLOGGED   one continuous tone, no gaps

The escalation is audible as an escalation. You can tell from the next room
whether the road is passable without looking at anything.

The rigs use PASSIVE piezos, so `BUZZER_PASSIVE` is 1. That matters more than it
sounds. A passive piezo is a bare disc with no oscillator in it, so a steady
level makes it tick once and then nothing at all. With the setting at 0 the
firmware was driving the pin correctly and printing `buzzer ON` 88 times in a
single run while the bench heard silence. The firmware was working and the
hardware was working and they simply did not match.

The ESP32 generates the 2700 Hz tone in hardware through LEDC, so the
continuous tone keeps sounding through anything that blocks the loop, including
a LoRa transmission.

## If it is silent

Listen at boot first. The firmware chirps once during `beacon_init()` and prints
this.

    buzzer self test: one chirp now

Heard the boot chirp and nothing during an alert. The wiring is fine and the
node never reached ELEVATED. Check the first word of the state, which is the
depth. In bringup the firmware announces the PATTERN whenever it changes, so
you can see what it thinks it is doing.

    buzzer: silent
    buzzer: beeping once a second (elevated)
    buzzer: CONTINUOUS (road under water)

Three or four of those lines in a whole trial. It used to print on every pin
edge, which was survivable at two chirps every ten seconds and would now be two
lines a second at ELEVATED, burying the sample lines you are actually reading.
Worse, WATERLOGGED has no edges at all once the tone starts, so the old print
would go quiet for hours and read as the buzzer having stopped.

No boot chirp. Run `tests/08_buzzer`, which drives the pin two ways ten seconds
apart and announces each one, then set the matching option.

    PHASE A: steady DC            -> active buzzer, set BUZZER_PASSIVE 0
    PHASE B: 2700 Hz square wave  -> passive piezo, set BUZZER_PASSIVE 1

If phase A is the one you hear and it still will not sound from the firmware,
the module is active-low, which means the firmware is silencing it during the
beep and sounding it the rest of the time. Set `BUZZER_ACTIVE_LOW` to 1. You
would probably have noticed a constant drone, so this is the less likely of the
two.

## Changing the patterns

Four numbers in `node/config.h`, an on-time and a period for each state.

    BUZZ_ELEVATED_ON_MS      150
    BUZZ_ELEVATED_PERIOD_MS  1000
    BUZZ_WATERLOG_ON_MS      1000
    BUZZ_WATERLOG_PERIOD_MS  1000

Continuous is the case where the on-time equals the period, so there is one rule
and no special branch. Build guards reject a period of zero, an on-time longer
than its period, and an ELEVATED pattern with no gap in it, since that would
make the two states sound identical.

The continuous tone costs roughly 30 mA for as long as the water is over the
road, and a blocked drain stays that way until somebody clears it. The node is
already awake and not sleeping at that point, so it is about a 40 percent
increase on an already-awake node rather than a new load. Give the waterlog
pattern a shorter on-time than its period to back it off.
