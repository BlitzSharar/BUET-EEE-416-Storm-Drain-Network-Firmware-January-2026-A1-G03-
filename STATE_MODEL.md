# State model: severity and trend

SHIPPED. This began as a proposal and is now what the firmware does.
`figures/StormDrain_State_Model.png` is the diagram. `CHANGES.md` records what moved and
what it was verified against.

Written against the field capture of 18 September.

The short version, in two parts.

Do not add RECEDING as a fifth state in the existing list. It breaks two pieces
of code that depend on the enum being ordered by severity, and it forces a
choice between showing depth and showing direction. Split the single state into
two small fields instead. Depth on one axis, motion on the other. A stall then
stops being a state and becomes what it actually is, a combination of the two.

And put the blockage decision where the information is. A node can measure that
water is not going down. It cannot measure why, because a blocked drain and a
drain being out-rained are identical from one level reading. The gateway has
every node's rain plate, the weather forecast and all three sites at once, so
the node measures and the gateway judges.

---

## 1. Why the obvious version does not work

The current enum is ordered, and two pieces of code rely on that ordering.

    enum Status : uint8_t { ST_NORMAL = 0, ST_RISING = 1, ST_WATERLOGGING = 2, ST_BLOCKAGE = 3 };

`beacon.cpp` lights the beacon with `if (st >= ST_WATERLOGGING)`. `status_engine.cpp`
picks the sampling cadence with `return candidate > current ? candidate : current`,
which means take whichever is more severe. Both read the numeric value as a
severity rank.

Append `ST_RECEDING = 4` and falling water becomes the most severe state in the
system. The beacon turns solid and the node switches to its fastest cadence
precisely when the situation is improving. Insert it lower and every other value
shifts, which changes the meaning of the status byte already in the packet and in
the archive.

There is a deeper problem than numbering. Water at 240mm and falling is still over
the road. If RECEDING replaces WATERLOGGING, the beacon has to go out while the
road is still flooded, or the label has to lie. A flat list forces that choice
because it is trying to carry two independent facts in one value.

---

## 2. The two fields

Depth and direction are independent. Encode them that way.

### Severity, from depth alone

    enum Severity : uint8_t {
      SEV_NORMAL      = 0,   // level < LEVEL_HIGH_MM
      SEV_ELEVATED    = 1,   // LEVEL_HIGH_MM <= level < LEVEL_SAFE_MM
      SEV_WATERLOGGED = 2    // level >= LEVEL_SAFE_MM
    };

Ordered, and it stays ordered. Every safety decision reads this and only this.
The beacon, the buzzer and `engine_road_unsafe()` are unchanged in spirit and
become easier to explain, because nothing about the direction of travel can ever
turn a warning off.

Note that `SEV_ELEVATED` is the old `ST_RISING` with an honest name. The old one
was reached by depth as well as by rate, so it never really meant rising.

### Trend, from motion alone

    enum Trend : uint8_t {
      TR_STEADY   = 0,
      TR_RISING   = 1,
      TR_RECEDING = 2,
      TR_STALLED  = 3    // elevated, rain stopped, no recession within the window
    };

`TR_STALLED` is the blockage condition, expressed as what it is. A drain that is
holding water it should have shed.

### Stall becomes derived, not stored

    bool stalled = (severity >= SEV_ELEVATED) && (trend == TR_STALLED);

This is exactly what `status_engine.cpp` already computes internally with
`recedeRunning`, `recedeStartLevel`, `recedeMinLevel` and `RECEDE_WINDOW_MS`. The
engine does the work today and then throws the result away by collapsing it into
a single label. Nothing new has to be measured.

### And the node stops there, which is the second half of the design

An earlier version of this rule had `&& !raining` on the end, so a node
suppressed its own stall while its rain plate was wet. That was wrong twice over.

It was wrong in practice, because only node 1 had a plate. Nodes 2 and 3 took
rainfall from a gateway broadcast, and while that was missing or stale the
engine was conservatively told it might be raining. Their detection was
therefore silently disabled whenever the gateway was off air, and nothing said
so. That cost an afternoon on the bench and would have cost a season in the
field.

It was wrong in principle, and that is the part that survives fitting a plate to
every node. A node cannot tell a blocked drain from one being out-rained,
because from a single level reading both are water that is not going down. What
separates them is rainfall AT THAT SITE and the shape of the event across the
network, and no node can ever see the second.

Gating at the node also throws away the one case worth seeing. Suppressing the
stall during rain means the gateway never gets to observe one drain stalled
while its two neighbours drain in the same downpour, which is the strongest
evidence of a local fault the network can produce.

So the node measures and the gateway judges:

    node      "water has been above LEVEL_HIGH_MM and has not receded by
               RECEDE_DROP_MM for RECEDE_WINDOW_MS"      a measurement

    gateway   "and it is dry here while the other two are draining,
               so that is a blockage"                    a verdict

The node reports its own plate as a separate field. It simply does not use it.

`gateway/blockage.cpp` produces one of four verdicts:

    none        the node is not stalled
    storm       stalled, raining, and every reporting node is stalled together
    suspected   stalled during rain while a neighbour still drains, or stalled
                with nothing available to corroborate the dryness
    confirmed   stalled with no rainfall to explain it, corroborated by a
                working forecast or by another node that also reads dry

Two guards are worth stating because both protect against a plate that has
corroded open, which reads dry for ever. Confirmation needs a second opinion, so
one plate cannot raise a callout alone. And two unanimously wet neighbours
outvote one dissenting dry reading, while a single disagreeing neighbour does
not, because that is the local variation the per-site plates exist to respect.

---

## 3. What it costs on the wire

Nothing. The packet is unchanged in length.

`lora_tx.cpp` currently writes `p[6] = (uint8_t)st`, using two bits of a byte.
Pack both fields into the same byte.

    p[6] = (uint8_t)((trend << 4) | severity);

The gateway unpacks and validates in `lora_rx.cpp`, replacing the single bound
check on line 55.

    uint8_t sev = p[6] & 0x0F;
    uint8_t trd = p[6] >> 4;
    if (sev > 2 || trd > 3) return false;

The archive gains one column. Since the SD card is not logging yet and there is
no field data worth preserving, change the format now and start clean rather than
carrying a compatibility shim nobody will ever need.

---

## 4. The trend must be computed over a window, not from the rate field

This matters more than the encoding, and the field capture proves it.

The transmitted `rate` comes from one interval, `level_rate_mm_min()` dividing the
change between two consecutive samples by the gap. On still water in the bucket
that produced swings of plus and minus 300mm per minute, and 46 of 322 samples
exceeded the range the packet byte can even carry. Driving a trend field from that
number would make it flap every sample.

Compute the trend from the span across a window of TIME instead.

    #define TREND_WINDOW_MS      60000UL  // 10 s in bringup
    #define TREND_DEADBAND_MM    8        // less movement than this is STEADY

A duration, never a sample count. The sampling interval changes with state, so a
deadband applied to N samples would mean 60 mm/min at the bringup cadence and
2 mm/min at the dry cadence, making the field most sensitive exactly when
nothing is happening.

Even as a duration it is not quite enough. The window closes at the first sample
at or after `TREND_WINDOW_MS`, so when the sampling interval is LONGER than the
window the span really covers the sampling interval, and `power.cpp` triples the
dry cadence on a low battery. A fixed 8 mm across 180 s is 2.67 mm/min against
8 mm/min everywhere else. The same inversion, arriving by a different route.

So the deadband is scaled by the time actually elapsed. It is a rate,
`TREND_DEADBAND_MM` per `TREND_WINDOW_MS`, and 8 mm across one minute and 24 mm
across three minutes are the same verdict. Past `TREND_STALE_FACTOR` windows the
two readings are too far apart for their difference to describe a rate at all,
and the engine re-anchors and reports STEADY rather than inventing a threshold
no real rise could clear.

`TREND_DEADBAND_MM` is set from measurement rather than taste. Across ten still
water stretches in the 18 September capture, spanning nine levels from 0 to
239 mm, the worst span seen over a window was 5 mm and the 95th percentile was
4 mm. 8 mm clears the worst observed noise by 1.6 times.

The window anchors on the absolute clock, not `millis()`, for the same reason
the recession timer does: `millis()` restarts at zero on every wake, so a span
computed from it across a sleep is arithmetic on two unrelated timebases.

---

## 5. What the operator sees

Two fields, eleven readouts, up from four, and only seven words to learn.

| level | steady | rising | falling | not draining |
|---|---|---|---|---|
| under 100 mm | `NORMAL` | `NORMAL RISING` | `NORMAL CLEARING` | cannot happen |
| 100 to 149 | `ELEVATED` | `ELEVATED RISING` | `ELEVATED CLEARING` | `ELEVATED BLOCKED` |
| 150 and up | `WATERLOG` | `WATERLOG RISING` | `WATERLOG CLEARING` | `WATERLOG BLOCKED` |

One depth word plus one motion word, and the motion word means the same thing
on every row. Three depths and four motions to learn rather than eleven
arbitrary names. The old table used CLEARING at shallow depth, DRAINING at knee
depth and WLOG DRAIN at road depth, three different words for one fact.

`NORMAL BLOCKED` cannot occur. The recession timer is only armed at or above
`LEVEL_HIGH_MM`, so shallow water is never called blocked however long it sits.

`WATERLOG CLEARING` and `WATERLOG BLOCKED` are the pair that carries the whole
argument of the project. Same depth, same reading, opposite meaning, and the old
firmware showed them identically.

BLOCKED is a LOCAL READ and the node cannot verify it. One drain holding water
looks identical whether it is blocked or being out-rained, so during a storm all
three nodes will display BLOCKED while the gateway correctly reports
`blockage = storm`.

That disagreement is the design working rather than a fault in it. The node
reports what it measured and the network decides what it means, which is the
reason three nodes exist instead of one. A thermometer can honestly say FEVER.
It cannot say FLU, because that needs to know whether half the office is also
ill.

---

## 6. Evidence from the 18 September capture

Of 130 samples published as WATERLOGGING, 21 were water that was actually falling.
The longest unbroken fall ran 20 seconds and appeared on the display as a static
label the entire time. An operator watching that screen could not tell a drain
that was emptying from one that was not, which is the single question the system
exists to answer.

---

## 7. What it cost, in the end

Both stages were done together rather than separately.

Firmware: `status_engine.h` and `.cpp` rewritten, plus `node.ino`, `power.cpp`,
`beacon.cpp`, `ui_local.cpp`, `lora_tx.cpp` on the node, and `lora_rx.h` and
`.cpp`, `sd_log.cpp`, `ui_local.cpp` on the gateway, plus the Blynk module.

Tests: `sim_test`, `diff_test`, `statespace_test`, `logic_test`, `orch_test`,
`fuzz_test`, `tx_wait_test` and `mutate.py`. That was most of the work, and it
was where the value came from. The differential oracle had to be rewritten from
the specification rather than from the source, and the state space model had to
carry the trend machine so the BFS still terminated.

Several bugs were found during the port and afterwards that the old suite could
never have seen.

A sticky `TR_STALLED` that let a stall survive the condition that produced it,
found by `fuzz_test` rather than by the differential oracle, which agreed with
the buggy engine because the reference had the same mistake written into it. Two
implementations agreeing proves they agree, not that they are right.

A long-stable backoff predicate that excluded waterlogging by name, which
stopped being correct the moment a stall shared a scheduling level with it.

And the one worth remembering, because every test in the suite passed straight
through it. `curTrend` was set to `TR_STALLED` BEFORE the debounce ran, and
`curTrend` is what goes on the air. The gateway recomputes the stall out of that
byte, so it saw the RAW stall: the very first sample to trip the recession rule
was transmitted on the change-triggered uplink and could be escalated to
CONFIRMED there and then. The debounced value never left the node. It gated
cadence and sleep and nothing else, so the three-sample filter contributed
nothing at all to the alert it existed to protect. Every host test missed it
because they all read `EngineOut.stalled`, which is the field the node uses
internally, rather than the byte it sends. An independent audit found it by
reading the code.

The debounce is now asymmetric on purpose. Raising a stall takes three agreeing
samples, because it ends in a callout to a named drain. Clearing one is
immediate, because what clears it is direct evidence against it.

---

## 8. One thing not to do

Do not let the trend influence the beacon or the buzzer. The temptation is to ease
the warning once the water starts falling, and it is wrong. The road is unsafe at a
depth, not at a direction. Keeping severity as the only input to the physical
warning is the property that makes the whole design defensible, and it is worth
stating in the report as a deliberate choice rather than leaving it implicit.


---

## 9. Verified

    838 assertions across 21 host suites          pass
    2,000,000 differential sequences              0 divergences
    1,589 reachable internal states               exhaustively explored, no traps
    24 node engine mutants                        24 killed
    25 gateway mutants (escalation and vote)      25 killed
    225 compile configurations                    pass

The state space search proves the point of the whole exercise directly. A
waterlogged drain that is DRAINING and a waterlogged drain that is STALLED are
both reachable, expressible states. Under the old enum neither of them was.
