# IoT Smart Storm-Drain Network

EEE 416 Microprocessors and Embedded Systems Laboratory, BUET, January 2026.
Section A1, Group 03.

Three sensor nodes and one gateway that tell a blocked storm drain apart from a
drain that is simply full because of heavy rain.

A water level sensor on its own cannot do this. A blocked drain and a working
drain in a downpour both hold water that is not going down. Each node here
reports when high water stops falling, and the gateway decides why. It checks
the drain's own rain plate, a rainfall forecast and what the other two drains
are doing at the same time, then gives one of four verdicts with the reason
attached.

| Verdict | Code | Meaning |
|---|---|---|
| none | 0 | The drain is not stalled |
| storm | 1 | Every drain is stalled in the rain, so no crew is needed |
| suspected | 2 | This drain is stalled while a neighbour still drains, or nothing backs up a dry reading |
| confirmed | 3 | This drain is stalled, it is not raining there, and a second source agrees |

## Hardware

Each node is an ESP32 DevKit V1 with an Ra-02 (SX1278) LoRa radio at 433 MHz,
a JSN-SR04T waterproof ultrasonic sensor, a resistive rain plate, an SSD1306
OLED and a passive buzzer. Node 2 also drives a road warning beacon. Every node
runs from two 18650 cells in parallel through an MT3608 boost converter set to
5 V, and the solar node charges its cells from a small panel through a CN3065
charger.

The gateway is a fourth ESP32 with the same radio, a microSD card on its own SPI
bus and an OLED. It joins WiFi to fetch a rainfall forecast from Open-Meteo and
to update a Blynk dashboard on a phone.

The pin maps are at the top of `node/config.h` and `gateway/config.h`.

## Repository layout

```
node/          node firmware, open node.ino
gateway/       gateway firmware, open gateway.ino
extras/        net_report_base.cpp, the gateway build without the dashboard
tests/         eight small sketches that check each part on its own
tests_host/    host test suite, runs on a laptop with no hardware
field_data/    bench captures from 19 and 21 September and the analysis scripts
tools/         scripts that redraw the diagrams in docs/figures
docs/          design notes, setup guides and figures
```

The documents in `docs/` are

* `STATE_MODEL.md` describes the two state machines and the gateway verdict
* `BRINGUP.md` is the bench procedure, part by part
* `CALIBRATION_AND_TESTING.md` covers the values set on each rig
* `BLYNK_SETUP.md` walks through the dashboard, all 22 datastreams included
* `DORMANCY_DESIGN.md` is a dry season power mode, designed but not built
* `CHANGES.md` records every fix and the test that now catches it

## Building

1. Install the ESP32 board package in Arduino IDE. Versions 2.x and 3.x both work, and 2.0.17 is the safe choice if anything fails to compile.
2. Install these libraries from the Library Manager.
   * LoRa by Sandeep Mistry
   * Adafruit SSD1306 and Adafruit GFX
   * ArduinoJson, version 7 (the gateway uses `JsonDocument`, which version 6 does not have)
   * Blynk by Blynk, version 1.3.5 (not the Khoi Hoang forks)
3. Copy `gateway/secrets.example.h` to `gateway/secrets.h` and fill in your hotspot name, its password and the three Blynk values. `secrets.h` is in `.gitignore`, so it stays on your machine.
4. In `node/config.h` set `NODE_ID` to 1, 2 or 3 for each board, and set `RAIN_WET_ADC` and `RAIN_DRY_ADC` for that board's plate using `tests/06_rain`.
5. Open `node/node.ino` or `gateway/gateway.ino` and upload.

`BRINGUP_MODE` in `node/config.h` ships as 0, the field build. Setting it to 1
gives the bench build, which shortens the stall window from 20 minutes to 2 and
reports every 10 s. Every result in `field_data/` was taken in the bench build.
The bench build uses more than the legal radio duty cycle, so return it to 0
before any outdoor use.

To run the gateway without the dashboard, copy `extras/net_report_base.cpp`
over `gateway/net_report.cpp`.

## Testing

```
bash tests_host/run_all.sh
```

The decision logic on both boards depends only on depth, time, the rain plates
and the forecast, so it is tested on a laptop with small stand in headers for
the Arduino libraries. The current run gives

```
838 assertions across 21 host suites     all pass
225 compile configurations               all pass
24 node engine mutants                   24 caught
25 gateway mutants                       25 caught
2,000,000 random steps against a separately written model, no disagreement
1,589 reachable internal states          explored, no state that cannot recover
```

`tests_host/compile_check.sh` runs the compile sweep alone in about ten
seconds. `tests_host/README.md` explains each suite.

## Results

On a 275 minute bench run the gateway logged 1,533 reports. In 42 of them two
drains were stalled at the same time and got different verdicts because their
rain plates differed. The median time from water holding above 120 mm to a
reported stall was 124 s against a 120 s bench window. Packet delivery was 89.5
to 91.3 percent per node. `field_data/README.md` explains how to reproduce
every figure.

## Team

| Name | Student ID |
|---|---|
| Siddhartha Sankar Das | 2106004 |
| Raad Sharar | 2106013 |
| Parag Kumar Kabiraj | 2106014 |
| Ibtesham Mehedul Tishan | 2106030 |

Course instructors Dr. Mohammad Ariful Haque and Abrar Assaeem Fuad.
