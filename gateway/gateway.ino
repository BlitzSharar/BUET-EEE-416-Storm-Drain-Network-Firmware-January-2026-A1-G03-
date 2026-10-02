// EEE 416 Storm-Drain Network: gateway firmware
//
// Design review response, implemented here:
//  - every received packet is archived to microSD before anything else, so the
//    dataset survives an internet outage
//  - RSSI, SNR and dropped-packet counts are recorded for the interference and
//    range study
//  - a periodic rainfall broadcast, for any node built without its own plate.
//    Replying to every uplink as well is off by default: every node carries a
//    plate now, and at SF8 that reply nearly doubled the network's airtime
#include "config.h"
#include "lora_rx.h"
#include "lora_bcast.h"
#include "blockage.h"
#include "node_digest.h"
#include "forecast.h"
#include "net_report.h"
#include "ui_local.h"
#include "sd_log.h"
#include "data_export.h"
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include <WiFi.h>
#include <time.h>

unsigned long tForecast = 0, tBcast = 0;

// esp_task_wdt_init() changed signature between board packages: ESP-IDF 4
// (arduino-esp32 2.x) takes (timeout_seconds, panic), ESP-IDF 5 (3.x) takes a
// config struct and the framework has usually initialised the timer already,
// so init returns ESP_ERR_INVALID_STATE and reconfigure is the correct call.
// Same wrapper as node/watchdog.cpp, so the gateway compiles on either core.
static void wdt_init_compat(uint32_t timeout_s, bool panic) {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms     = timeout_s * 1000U;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic  = panic;
  if (esp_task_wdt_init(&cfg) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&cfg);
  }
#else
  esp_task_wdt_init(timeout_s, panic);
#endif
}

void setup() {
  Serial.begin(115200);
  // Arm the watchdog first. Unlike a node, the gateway has no local beacon to
  // fall back on, so a silent hang here is invisible until someone checks the
  // dashboard. The reset cause is logged so a reboot is never a mystery.
  esp_reset_reason_t why = esp_reset_reason();
#if WDT_ENABLED
  wdt_init_compat(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
#endif
#if BRINGUP_MODE
  Serial.println();
  Serial.println("=====================================================");
  Serial.println("  GATEWAY BRING-UP MODE.  Heartbeat every 5 s.");
  Serial.println("  Set BRINGUP_MODE to 0 in config.h for real use.");
  Serial.println("=====================================================");
#endif
  ui_init();
  lora_gw_init();
  sdlog_init();          // local archive first: it must work without a network
  net_init();
  // Announce readiness unconditionally, not only in bring-up mode. A silent
  // gateway that is working looks exactly like one that has hung, and the
  // radio parameters printed here are the first thing to compare against the
  // node when packets are not arriving.
  Serial.printf("gateway ready: %ld Hz, SF%d, sync 0x%02X, SD %s\n",
                (long)LORA_FREQ_HZ, LORA_SF_DEFAULT, LORA_SYNC_WORD,
                sdlog_ok() ? "logging" : "unavailable, serial only");
  // Say what the monitor will and will not do, once, at boot. Silence that has
  // been explained is a design choice. Silence that has not been explained is
  // the bug this project already spent a day on.
  //
  // No depth numbers here on purpose. LEVEL_HIGH_MM and LEVEL_SAFE_MM live in
  // node/config.h and are set PER SITE, so the gateway does not have them and
  // must not keep its own copy. The node is the classifier; the gateway counts
  // votes. A second copy of a threshold here is the drift this design spent
  // three audits removing.
  // NO ESCAPED QUOTES IN THIS FILE, EVER, AND NOT AS A STYLE PREFERENCE.
  //
  // The Arduino IDE runs a ctags based prototype generator over the sketch
  // before the compiler ever sees it, and that parser tracks string boundaries
  // itself rather than using the real lexer. A backslash-escaped quote inside a
  // string literal can make it lose the end of the string, after which every
  // brace in the file reads as being inside one. It does not report an error.
  // It hangs, with an empty output pane and "Compiling sketch..." on screen,
  // which looks like a broken toolchain and is not one.
  //
  // An earlier version of this banner wrapped the two severity words in escaped
  // double quotes and hung the IDE on a fresh extract. A comment is no safer
  // than code here, because the same parser reads both, so this paragraph
  // deliberately contains no escape either. Plain println calls, one per line,
  // are the boring thing that cannot bite.
  Serial.print  ("monitor: one line per ");
  Serial.print  (DIGEST_SAMPLES);
  Serial.println(" packets per node, reporting the severity seen most often.");
  Serial.println("         elevated reads 'partial blockage', waterlogged reads");
  Serial.println("         'complete blockage', and a normal node is not reported");
  Serial.println("         at all. Silence between heartbeats means all three are");
  Serial.println("         normal. Every packet is still archived in full.");
  // NTP, so the archive carries a wall clock time and not only milliseconds
  // since boot. Non blocking: it fills in whenever the network comes up, and
  // sd_log writes an empty timestamp field until then.
  configTime(TZ_OFFSET_SEC, TZ_DST_OFFSET_SEC, NTP_SERVER);
  forecast_update();     // no-op until WiFi associates; loop() retries
  dexport_begin();       // archive download over WiFi, and a serial dump
  { char d[64];
    int wrote = snprintf(d, sizeof d, "gateway started, reset reason %d", (int)why);
    if (wrote < 0 || (size_t)wrote >= sizeof d) { d[sizeof d - 1] = '\0'; }
    sdlog_event("boot", d); }
}

void loop() {
  NodeReport rep;
  if (lora_gw_poll(&rep)) {
    // REPLY FIRST. A sleeping node listens only in a short window right after
    // its own uplink, and everything else the gateway does here is slow: an SD
    // flush is on the order of 100 ms and a dashboard publish over WiFi can be
    // several hundred. Doing that work before replying pushes the broadcast
    // past the node's window at higher spreading factors and the node silently
    // never receives its rainfall context, which disables the blockage rule.
    // The timing budget is in tests_host/timing_test.cpp.
    //
    // OFF by default now. Every node carries its own plate and the node's rule
    // takes no rain input, so nothing on a shipping node reads this reply, and
    // at SF8 it roughly doubles the network's airtime: 26.7 s/hour of uplinks
    // becomes 49.7 s/hour against a 36 s/hour budget. See
    // GW_REPLY_TO_EVERY_UPLINK in config.h. The periodic broadcast below still
    // goes out for any plate-less node that is awake and listening.
    //
    // Rain for the broadcast comes from ANY reporting plate plus the forecast,
    // not from node 1 alone. Every node carries a plate now, so a node 1 that
    // is offline or whose plate has corroded no longer decides what the rest of
    // the network is told about the weather. blockage_rain_believed() is the
    // same predicate the escalation uses, so the broadcast and the verdict can
    // no longer disagree about whether it is raining.
#if GW_REPLY_TO_EVERY_UPLINK
    bcast_send(blockage_rain_believed(), forecast_mm_x10());
#endif

    // Now the slow work, with the node's window already satisfied.
    //
    // The escalation runs BEFORE the archive so the verdict and its reason are
    // written into the same row as the reading that produced them. A stall
    // recorded without the rainfall context that explained it is not evidence
    // anybody can use afterwards.
    blockage_note_report(rep);
    BlockageVerdict v = blockage_verdict(rep.node_id);

    // THE MONITOR REPORTS CONDITION, NOT PACKETS.
    //
    // One line per DIGEST_SAMPLES packets from a node, carrying the severity
    // that arrived most often inside that group, and nothing at all for a node
    // whose group votes NORMAL. Three nodes at once, live, without three
    // laptops.
    //
    // Everything left out here is still ARCHIVED. sdlog_report() below writes
    // every packet with its trend, its verdict, the reason string, RSSI, SNR
    // and the dropped count, unchanged. This decides what is worth a human's
    // attention on a bench, not what is worth keeping.
    //
    // The vote and its tie rule live in node_digest.cpp so they can be tested
    // on a laptop. Ties there break toward the MORE severe, which matters more
    // than it sounds: five samples across three categories tie constantly for a
    // drain sitting near LEVEL_SAFE_MM.
    Digest dg;
    if (digest_note(rep, &dg)) {
      const char* words = digest_words(dg.severity);
      // Empty means the group voted NORMAL. Say nothing, by request.
      if (words[0] != '\0') {
        Serial.printf("N%u level=%dmm rate=%d rain=%s  %s\n",
                      dg.node_id, dg.level_mm, (int)dg.rate,
                      dg.rain ? "wet" : "dry", words);
      }
    }
    sdlog_report(rep, v, blockage_reason(rep.node_id));
    net_publish(rep, v);
    ui_show_last(rep, v);
  }

  // Retry on the short interval until a fetch has actually succeeded, then
  // settle to the normal one. The first call in setup() always no-ops because
  // WiFi.begin() has not associated yet, and waiting the full 30 minutes left
  // the network with rainNow stuck false, which is the false blockage direction.
  // THE BACKOFF FORECAST_RETRY_MAX_MS DESCRIBES, NOW ACTUALLY IMPLEMENTED.
  //
  // This used to be a flat 60 s retry. The comment on FORECAST_RETRY_MAX_MS
  // spelled out why that is not good enough and the constant was then never
  // referenced anywhere, so the scenario it warned about was live: WiFi
  // associated to a hotspot with mobile data off reports CONNECTED, so
  // net_have_internet() is true and the gateway retried every 60 s for ever.
  // Each attempt blocks this single-threaded loop for up to
  // FORECAST_HTTP_TIMEOUT_MS, lora_gw_poll() is not called during it, and the
  // SX1276 buffers one frame, so roughly one uplink in six was lost
  // permanently. Reply-first ordering cannot help with that: it orders the work
  // inside an iteration, it does not bound how long until the iteration starts.
  unsigned long fcInterval;
  if (forecast_ok()) {
    fcInterval = FORECAST_INTERVAL_MS;
  } else {
    uint32_t streak = forecast_fail_streak();
    if (streak > 8) streak = 8;                    // 60 s << 8 is already past the cap
    unsigned long backoff = FORECAST_RETRY_MS << streak;
    fcInterval = backoff > FORECAST_RETRY_MAX_MS ? FORECAST_RETRY_MAX_MS : backoff;
  }
  // No internet as an access point, so there is nothing to fetch. Retrying
  // would just burn the loop and log failures every few minutes.
  if (net_have_internet() && millis() - tForecast >= fcInterval) {
    tForecast = millis();
    // Feed before the fetch as well as after the loop. The fetch is the one
    // call in this loop whose duration depends on a third party, and a healthy
    // gateway should not reset because a weather server was slow. If the fetch
    // hangs past its own timeout the loop still stops feeding and the watchdog
    // does its job.
#if WDT_ENABLED
    esp_task_wdt_reset();
#endif
    forecast_update();
  }

  // SAY WHAT STATE THE FORECAST IS IN, ON ITS OWN CADENCE.
  //
  // This used to be inside the fetch block, so the only way to hear about the
  // forecast more often was to hit Open Meteo more often. The fetch interval is
  // a courtesy to them. The report interval is for whoever is watching the
  // monitor. They are now separate.
  //
  // It has to be said at all because "the forecast says dry" and "the forecast
  // has not worked since Tuesday" produce the identical verdict and used to look
  // identical on the monitor. A dry forecast is what turns a stall into a
  // CONFIRMED blockage, so the difference is the whole ballgame.
  //
  // ALSO WRITTEN INTO THE ARCHIVE, on change and on the first reading after
  // boot. Without that, a CONFIRMED verdict in the CSV cannot be shown to have
  // had forecast backing rather than only a dry neighbouring plate, because
  // dryness_corroborated() accepts either and the reason string says the same
  // thing for both. A verdict you cannot audit afterwards is not evidence.
  {
    static unsigned long tFcSay = 0;
    static bool everSaid = false;
    static int lastState = -2;              // -2 nothing yet, -1 unusable, 0 dry, 1 wet
    uint32_t age = forecast_age_ms();
    int state = (age == FORECAST_AGE_NONE) ? -1 : (forecast_rain_now() ? 1 : 0);

    bool due = !everSaid || (millis() - tFcSay >= FORECAST_REPORT_MS);
    bool changed = (state != lastState);

    if (due || changed) {
      tFcSay = millis();
      everSaid = true;
      char line[96];
      if (state < 0) {
        snprintf(line, sizeof line, "none usable, %lu consecutive failures",
                 (unsigned long)forecast_fail_streak());
      } else {
        snprintf(line, sizeof line, "%s, %lu.%lu mm/h, %lu min old",
                 state ? "raining" : "dry",
                 (unsigned long)(forecast_mm_x10() / 10),
                 (unsigned long)(forecast_mm_x10() % 10),
                 (unsigned long)(age / 60000UL));
      }
      Serial.printf("forecast: %s\n", line);
      // Archive it on a CHANGE only, not on every report. A row every 30
      // minutes saying the same thing is noise in a file whose whole job is to
      // be analysable afterwards. A row at every transition is the timeline you
      // actually want to join a verdict against.
      if (changed) sdlog_event("forecast", line);
      lastState = state;
    }
  }

  // Periodic broadcast as well, for any node that is awake and listening.
  if (millis() - tBcast >= BCAST_INTERVAL_MS) {
    tBcast = millis();
    bcast_send(blockage_rain_believed(), forecast_mm_x10());
  }

  // LISTENING HEARTBEAT, IN EVERY BUILD.
  //
  // This used to be bring-up only, and that was survivable while the gateway
  // printed a line for every packet. It is not survivable now. A node voting
  // NORMAL is reported as nothing at all, and the verdict line has gone, so on
  // a healthy network this heartbeat is the ONLY output the gateway produces.
  // Without it in the field build, a working gateway, a gateway with a dead
  // radio and a gateway that has hung are three identical blank screens.
  //
  // Every five seconds on the bench where you are watching it, once a minute in
  // the field where you are not.
  static unsigned long tBeat = 0;
  if (millis() - tBeat >= HEARTBEAT_MS) {
    tBeat = millis();
    // Accepted, missed and rejected, because those three failures need three
    // different answers. Accepted climbing means the link is fine. Rejected
    // climbing with accepted stuck means frames are arriving and being thrown
    // away, so the fault is a build or format mismatch, not the radio. Both
    // stuck at zero means nothing is reaching the gateway at all.
    Serial.printf("listening on %ld Hz SF%d sync 0x%02X, "
                  "accepted %lu, missed %lu, rejected %lu\n",
                  (long)LORA_FREQ_HZ, LORA_SF_DEFAULT, LORA_SYNC_WORD,
                  (unsigned long)rx_total_received(),
                  (unsigned long)rx_total_dropped(),
                  (unsigned long)rx_total_rejected());
  }

  net_check_offline();
  net_run();

  // Last, deliberately. Serving a file blocks, so it must never sit between
  // receiving a packet and replying to the node.
  dexport_poll();

  // Fed only after a full loop pass that actually serviced the radio and the
  // network, so a loop stuck before this point still triggers a reset.
#if WDT_ENABLED
  esp_task_wdt_reset();
#endif
}
