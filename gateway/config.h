#ifndef CONFIG_H
#define CONFIG_H

// Set to 1 while proving the link. Prints a listening heartbeat so you can
// tell a silent gateway from a hung one, and shortens the offline timeout so
// a missing node is reported in two minutes rather than twenty.
//
// Wrapped in #ifndef so the build system, or tests_host/compile_check.sh, can
// override it without editing this file. The node config has always worked
// this way and the gateway now matches.
#ifndef BRINGUP_MODE
#define BRINGUP_MODE       0
#endif

#if BRINGUP_MODE != 0 && BRINGUP_MODE != 1
#error "BRINGUP_MODE must be 0 or 1"
#endif

// ---------------- Pins ----------------
#define PIN_LORA_SCK       18
#define PIN_LORA_MISO      19
#define PIN_LORA_MOSI      23
#define PIN_LORA_NSS       5
#define PIN_LORA_RESET     14
#define PIN_LORA_DIO0      26
// microSD runs on a SECOND SPI bus (HSPI), not shared with the radio.
// Sharing one bus between the SX1278 and an SD card is a well documented
// failure mode: the two libraries fight over bus configuration and the SD card
// typically stops initialising. These four pins are free on the gateway and
// none of them is a strapping or input-only pin.
#define PIN_SD_SCK         25
#define PIN_SD_MISO        27
#define PIN_SD_MOSI        32
#define PIN_SD_CS          33
#define PIN_SIM_RX         16     // optional SIM800L (needs stable 4V 2A supply)
#define PIN_SIM_TX         17
#define PIN_OLED_SDA       21
#define PIN_OLED_SCL       22

// ---------------- Radio (must match the nodes) ----------------
#define LORA_FREQ_HZ       433000000L  // must match node/config.h exactly
#define LORA_SYNC_WORD     0x37
#define LORA_SF_DEFAULT      8       // tune from the week 5 range test
#define LORA_SF_MAX          8       // above this the duty cycle budget fails
#define BCAST_ID           0xFF
#define NUM_NODES          3

// ---------------- Blockage escalation, gateway side ----------------
// The node reports STALLED on level alone. The gateway decides whether that is
// a blockage or a storm, because only the gateway can see rainfall and all
// three nodes at once. See gateway/blockage.h.

// Every node carries its own resistive plate, so rainfall is measured per site
// rather than one reading standing in for all three. There is no "rain source
// node" any more: the knob that used to name one was removed rather than left
// sitting there looking as though it still selected something.

// How long after the plate last read wet the gateway keeps crediting rain for
// a stalled drain. A drain legitimately takes time to clear once rain stops, so
// declaring a blockage the moment the last drop falls would be a false alarm.
// Sits above RECEDE_WINDOW_MS on purpose: the node needs that long to declare
// a stall in the first place, so a shorter memory would never bite.
#ifndef RAIN_MEMORY_MS
#if BRINGUP_MODE
// Scaled with the node's recession window, which bring-up also shortens. At the
// field value of 30 minutes against a 2 minute bench window, a plate wetted once
// keeps its site counted as raining for fifteen consecutive trials, so the dry
// case needed to confirm a blockage could never be reached on the bench.
#define RAIN_MEMORY_MS       (3UL * 60UL * 1000UL)
#else
#define RAIN_MEMORY_MS       (30UL * 60UL * 1000UL)
#endif
#endif

// Forecast precipitation, in tenths of a millimetre per hour, at or above which
// the gateway treats the hour as wet. 0.2 mm/h is drizzle.
#ifndef FORECAST_WET_X10
#define FORECAST_WET_X10     2
#endif

// ---------------- Network ----------------
// WIFI_MODE picks how the gateway uses its radio.
//
//   0  STATION. Joins your hotspot or router. Internet, so the forecast and
//      the dashboard work. The address changes every time it reconnects, so
//      you have to read it off the serial monitor.
//
//   1  ACCESS POINT. The gateway is the network and the laptop joins it. No
//      router, no captive portal, no client isolation, no 5 GHz problem, and
//      the address is always 192.168.4.1. No internet, so no forecast and no
//      dashboard.
//
//   2  BOTH. Runs as an access point and joins your hotspot at the same time.
//      This is the one to use. The laptop always reaches 192.168.4.1 whatever
//      the hotspot is doing, and the forecast and dashboard work whenever the
//      hotspot is up. If the hotspot drops mid-demonstration the access point
//      stays up and you lose only the dashboard, not the gateway.
//
//      One quirk worth knowing: both interfaces share one radio, so the access
//      point is forced onto whatever channel the hotspot is using. That is
//      normal and needs nothing from you.
#ifndef WIFI_MODE
#define WIFI_MODE          2
#endif

#if WIFI_MODE != 0 && WIFI_MODE != 1 && WIFI_MODE != 2
#error "WIFI_MODE must be 0 (station), 1 (access point) or 2 (both)"
#endif

// Kept as an alias so older code and comments still read correctly.
#define WIFI_AP_MODE       (WIFI_MODE == 1)

// Used when WIFI_AP_MODE is 1. Anything you like; the password must be at
// least 8 characters or the ESP32 will refuse to start the access point.
// Browse to http://<this>.local/ instead of an IP address.
#define MDNS_NAME          "stormdrain"

#define AP_SSID            "stormdrain-gw"
#define AP_PASS            "drain2026"

// The hotspot the gateway joins for internet. Used when WIFI_MODE is 0 or 2.
//
// THE NAME MUST MATCH WHAT THE PHONE SHOWS, CHARACTER FOR CHARACTER. iOS names
// the hotspot after the device, so check Settings, General, About, Name before
// trusting this line. Two things catch people out. If the device name contains
// an apostrophe, iOS uses a RIGHT SINGLE QUOTATION MARK and not the ASCII one
// on your keyboard, and the two do not match. And a renamed phone keeps serving
// the old name until the hotspot is toggled off and on.
//
// Turn on Settings, Personal Hotspot, Maximize Compatibility. That forces the
// hotspot to 2.4 GHz. The ESP32 has no 5 GHz radio at all, so without it the
// gateway reports "network not found" while your laptop sees the hotspot fine,
// which looks like a gateway fault and is not one.
//
// Leave mobile data ON. A hotspot with data off still associates, so
// WiFi.status() reports CONNECTED, net_have_internet() returns true, and every
// forecast fetch then blocks for its full timeout and fails. That is the exact
// scenario FORECAST_RETRY_MAX_MS exists for.
// WIFI_SSID and WIFI_PASS live in secrets.h, next to the Blynk credentials.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif
// current=precipitation, not hourly[0].
//
// The old URL asked for hourly precipitation with no timezone parameter and
// read element [0], which is the 00:00 to 01:00 slot in GMT, so 06:00 to 07:00
// in Dhaka. It is a fixed value for the whole calendar day and changes only at
// the UTC date rollover, however often the gateway fetches it. A dry morning
// therefore reported dry all through an afternoon downpour, and the gateway
// used that to CONFIRM blockages that were only rain.
#define FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=23.81&longitude=90.41&current=precipitation&timezone=Asia%2FDhaka"

// ---------------- Timing ----------------
// Whole-transaction bound on the weather fetch. It happens inside loop() and
// before the watchdog is fed, so its worst case must stay well inside
// WDT_TIMEOUT_S. Derived rather than typed for the same reason the node's
// watchdog timeout is: a fixed number here can silently outgrow the timeout.
#define FORECAST_HTTP_TIMEOUT_MS  ((WDT_TIMEOUT_S * 1000UL) / 6UL)

#if BRINGUP_MODE
// Scaled with RAIN_MEMORY_MS and with the node's recession window, for the same
// reason. rain_at() short-circuits on the forecast, so one wet Open-Meteo
// reading at the field interval would make CONFIRMED unreachable for fifteen
// consecutive bench trials, and one dry reading would corroborate dryness for
// sixty. The plate evidence was scaled and this, which does the identical job,
// was not.
#define FORECAST_INTERVAL_MS  (3UL * 60UL * 1000UL)
#else
#define FORECAST_INTERVAL_MS  (30UL * 60UL * 1000UL)
#endif
// Until the first successful fetch, retry on this shorter interval. setup()
// calls forecast_update() immediately after the non blocking WiFi.begin(), so
// the link is never up yet and that first attempt always returns having done
// nothing. On the 30 minute interval alone the gateway then ran for half an
// hour with rainNow stuck at its false default, which is the direction that
// causes a false blockage alarm, and it happens on every reboot including the
// ones you do on demonstration day.
#define FORECAST_RETRY_MS     (60UL * 1000UL)

// Ceiling for the retry interval, which doubles on each consecutive failure.
// WiFi.status() reports CONNECTED for a hotspot with mobile data off or a
// router with no WAN, so net_have_internet() is true and the gateway retries
// for ever. Each attempt blocks the loop for up to FORECAST_HTTP_TIMEOUT_MS,
// and during that block lora_gw_poll() is not called at all. The SX1276 buffers
// one frame, so with three nodes on the 30 s event cadence a 10 s stall drops
// most of what arrives inside it and pushes the broadcast far past the node's
// 1500 ms receive window. Backing off turns a permanent packet loss into a
// brief one every quarter of an hour.
#define FORECAST_RETRY_MAX_MS (15UL * 60UL * 1000UL)

// HOW OLD A FORECAST MAY BE AND STILL COUNT AS EVIDENCE.
//
// There was no answer to this at all. rainNow, mm_x10 and the ever-succeeded
// flag were plain statics with no timestamp, a failed fetch deliberately left
// the previous value in place, and when WiFi dropped nothing was fetched at
// all. So one successful fetch, ever, was believed for the rest of the
// gateway's life. Both directions are bad and both are silent:
//
//   last fetch WET   every node counts as raining for ever, so CONFIRMED
//                    becomes unreachable and blockage detection is off
//   last fetch DRY   dryness is corroborated for ever, so one lone dry plate
//                    is enough to confirm a blockage and raise a callout
//
// The per-plate evidence already expires after RAIN_MEMORY_MS. This is the
// same idea for the other source. Four ordinary intervals, so three fetches
// have to fail in a row before the forecast stops counting.
#define FORECAST_STALE_MS     (4UL * FORECAST_INTERVAL_MS)
#if FORECAST_STALE_MS <= FORECAST_INTERVAL_MS
#error "A forecast would go stale before the next scheduled fetch could refresh it"
#endif

// HOW OFTEN THE FORECAST STATE IS SAID OUT LOUD, WHICH IS NOT HOW OFTEN IT IS
// FETCHED.
//
// These were the same number and that was wrong in both directions. Tying the
// report to the fetch meant the field build said nothing about the forecast for
// 30 minutes at a time, and the only way to make it talk more often was to hit
// somebody else's weather API more often.
//
// The fetch interval is a courtesy to Open Meteo. The report interval is for
// whoever is watching the monitor. They have nothing to do with each other.
#if BRINGUP_MODE
#define FORECAST_REPORT_MS    (30UL * 1000UL)     // bench, you are watching
#else
#define FORECAST_REPORT_MS    (30UL * 60UL * 1000UL)
#endif

#define BCAST_INTERVAL_MS     (60UL * 1000UL)

// HOW OFTEN THE GATEWAY PROVES IT IS ALIVE.
//
// The monitor now reports a node's CONDITION rather than its packets: one line
// per DIGEST_SAMPLES packets, and nothing at all for a node whose group votes
// NORMAL. On a healthy network that means the heartbeat is the only thing the
// gateway says, so it runs in every build rather than bring-up only. A silent
// gateway and a dead one have looked identical once already in this project and
// it cost most of a day.
#if BRINGUP_MODE
#define HEARTBEAT_MS          5000UL      // you are watching it
#else
#define HEARTBEAT_MS          60000UL     // you are not
#endif

// HOW MANY PACKETS GO INTO ONE MONITOR LINE.
//
// The reported severity is the one that arrived most often across this many
// packets from that node, counted per node so a busy node cannot close a quiet
// node's group early. See gateway/node_digest.h for why this is a vote and not
// an arithmetic mean, and why a tie breaks toward the more severe.
#ifndef DIGEST_SAMPLES
#define DIGEST_SAMPLES        5
#endif

// REPLY TO EVERY UPLINK? No, by default, and the reason is airtime.
//
// The gateway used to answer each uplink immediately with a 5 byte rainfall
// broadcast, so a sleeping node could catch it in the short window right after
// its own transmission. That was necessary when nodes 2 and 3 had no rain plate
// and took rainfall from the gateway. Every node carries a plate now and the
// node's own rule takes no rain input at all, so nothing on a shipping node
// reads that reply.
//
// It is not free. At SF8 with three nodes on the 30 s event cadence the reply
// roughly doubles the network's airtime, from 26.7 s/hour to 49.7 s/hour, and a
// 1 percent duty cycle is 36 s/hour. The whole network was 138 percent of its
// budget for a downlink no node was listening to. tests_host/timing_test.cpp
// now computes this against the shipping configuration and fails if it does not
// fit.
//
// Set this to 1 only for a deployment that actually includes a node built with
// NODE_HAS_RAIN 0, and check the duty cycle again when you do.
#ifndef GW_REPLY_TO_EVERY_UPLINK
#define GW_REPLY_TO_EVERY_UPLINK 0
#endif

// ---------------- Wall clock ----------------
// NTP, so the archive carries a real timestamp and not only milliseconds since
// the gateway last booted. Bangladesh Standard Time is UTC+6 with no DST.
#define NTP_SERVER            "pool.ntp.org"
#define TZ_OFFSET_SEC         (6 * 3600)
#define TZ_DST_OFFSET_SEC     0
#if BRINGUP_MODE
// Two minutes, not one. A bring-up node reports every 10 s, so this is still a
// dozen missed reports and plainly offline, and it has to stay clear of
// BCAST_INTERVAL_MS or the gateway declares a node offline at the same instant
// it broadcasts. At exactly 60 s against a 60 s broadcast that invariant was
// false, and nothing saw it because invariants_gateway_test.cpp only ever ran
// in production timing.
//
// Buying the invariant by shortening the broadcast instead was the obvious
// move and the wrong one: four times the beacon airtime takes SF8 from 85 to
// 117 percent of a 1 percent duty cycle, which the timing test catches.
#define NODE_TIMEOUT_MS       (2UL * 60UL * 1000UL)    // 2 min during bring-up
#else
#define NODE_TIMEOUT_MS       (20UL * 60UL * 1000UL)
#endif  // longer, since dry nodes
                                                       // now report every 15 min
// ---------------- Watchdog ----------------
// The gateway never sleeps, so a hang here silences the entire network with no
// local warning anywhere. Timeout allows for a slow forecast fetch over WiFi.
#define WDT_ENABLED          1
#define WDT_TIMEOUT_S        60

// ---------------- Local archive ----------------
#define SD_FLUSH_EVERY        10     // flush the file after this many records
#define SD_LOG_PATH           "/stormdrain.csv"
// SPI clock for the card. 4 MHz is the right default for breadboard jumper
// leads; 16 MHz gives intermittent init failures that look like a bad card.
#define SD_SPI_HZ             4000000
#endif
