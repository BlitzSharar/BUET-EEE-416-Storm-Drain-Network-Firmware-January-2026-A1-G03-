// DASHBOARD BUILD. Rebuilt 21 September as base + Blynk, rather than as a
// partial copy of the base. The partial copy silently lost three things and
// every one surfaced at the bench: net_have_internet() was never defined so
// it would not link, MDNS.begin() was never called so stormdrain.local could
// not resolve, and the access point banner and its failure check were gone.
// extras/net_report_base.cpp stays the no-dashboard fallback.
//
// The Blynk template ID, template name and auth token come from secrets.h,
// which is kept out of the repository. Copy secrets.example.h to secrets.h and
// fill it in. Without secrets.h the build falls back to the example file so
// the code still compiles, but the dashboard will not connect.

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

#define BLYNK_PRINT Serial

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <BlynkSimpleEsp32.h>
#include "config.h"
#include "net_report.h"
#include "sd_log.h"
#include "blockage.h"
#include "forecast.h"
#if WDT_ENABLED
  #include <esp_task_wdt.h>
#endif

static uint32_t lastSeen[NUM_NODES + 1] = {0};
static bool     offline[NUM_NODES + 1]  = {false};
static uint8_t  lastStatus[NUM_NODES + 1] = {0};

// Is there a route to the internet right now? The forecast and the dashboard
// ask this rather than checking the mode, so mode 2 behaves correctly whether
// or not the hotspot happens to be up at that moment.
bool net_have_internet() {
#if WIFI_MODE == 1
  return false;
#else
  return WiFi.status() == WL_CONNECTED;
#endif
}

void net_init() {
  // A name on the network beats an IP address nobody can remember. The router
  // shows this instead of esp32-1a2b3c.
  WiFi.setHostname(MDNS_NAME);

  // The ESP32 modem-sleeps by default in station mode, which adds a few
  // hundred milliseconds to every web request and makes the archive page feel
  // broken. The gateway runs on USB, so there is nothing to save here.
  WiFi.setSleep(false);

#if WIFI_MODE == 1
  WiFi.mode(WIFI_AP);
#elif WIFI_MODE == 2
  WiFi.mode(WIFI_AP_STA);
#else
  WiFi.mode(WIFI_STA);
#endif

#if WIFI_MODE == 1 || WIFI_MODE == 2
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println();
  Serial.println("=====================================================");
  if (ok) {
    Serial.printf("  ACCESS POINT: %s\n", AP_SSID);
    Serial.printf("  password:     %s\n", AP_PASS);
    Serial.print ("  browse to:    http://");
    Serial.print (WiFi.softAPIP());
    Serial.println("/");
  } else {
    Serial.println("  ACCESS POINT FAILED. The password needs 8 characters or more.");
  }
  Serial.println("=====================================================");
#endif

#if WIFI_MODE == 0 || WIFI_MODE == 2
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi: connecting to \"%s\" for internet\n", WIFI_SSID);

  // config() then connect() in the loop, never Blynk.begin(), which blocks
  // until it connects. The watchdog is already armed by now, so a wrong token
  // used to give a gateway that reset every minute with nothing on serial.
  Blynk.config(BLYNK_AUTH_TOKEN);
  Serial.println("Blynk: configured, connecting in the background");
  // Not waiting here on purpose. Blocking in setup() for a hotspot that may
  // not exist would stop the gateway ever reaching loop(), and receiving
  // packets matters more than the network does.
#endif

  // Resolves on both interfaces, so http://stormdrain.local/ works whichever
  // network the laptop happens to be on and survives the station address
  // changing between sessions.
  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("also reachable at http://%s.local/\n", MDNS_NAME);
  }
}

void net_run() {
#if WIFI_MODE == 1 || WIFI_MODE == 2
  static uint8_t lastClients = 255;
  uint8_t n = WiFi.softAPgetStationNum();
  if (n != lastClients) {
    lastClients = n;
    Serial.printf("AP: %u device%s connected\n", n, n == 1 ? "" : "s");
  }
#endif

#if WIFI_MODE == 0 || WIFI_MODE == 2
  // Report each change once. A gateway that silently fails to associate looks
  // identical to one that is working, and that ambiguity costs bench time.
  static wl_status_t last = WL_NO_SHIELD;
  static unsigned long t0 = 0, tSaid = 0;
  static bool warned = false;
  wl_status_t s = WiFi.status();
  // A station that cannot associate cycles between DISCONNECTED and
  // NO_SSID_AVAIL every couple of seconds. Printing on every transition buries
  // the packet lines the operator is actually watching for.
  bool quiet = (s != WL_CONNECTED) && (millis() - tSaid < 15000UL);
  if (s != last && !quiet) {
    tSaid = millis();
    last = s;
    switch (s) {
      case WL_CONNECTED:
        Serial.print("WiFi: internet up, IP ");
        Serial.print(WiFi.localIP());
        Serial.printf(", RSSI %d dBm\n", WiFi.RSSI());
        warned = false;
        break;
      case WL_NO_SSID_AVAIL:
        Serial.println("WiFi: network not found. Check WIFI_SSID. The ESP32 is "
                       "2.4 GHz only, so turn on Maximize Compatibility on an iPhone.");
        break;
      case WL_CONNECT_FAILED:
        Serial.println("WiFi: rejected. Check WIFI_PASS.");
        break;
      case WL_DISCONNECTED:
        Serial.println("WiFi: internet down, retrying. The access point is unaffected.");
        t0 = millis();
        break;
      default: break;
    }
  }
  if (s != WL_CONNECTED && !warned && t0 && millis() - t0 > 20000UL) {
    warned = true;
    Serial.println("WiFi: still no internet after 20 s. A captive portal login "
                   "or a 5 GHz only network are the usual reasons.");
  }

  // Retry on a timer, not every loop. connect() with no network blocks for its
  // full timeout each time, which is long enough to delay the reply a sleeping
  // node is waiting for.
  static bool wasUp = false;
  static unsigned long tRetry = 0;
  bool up = Blynk.connected();
  if (up != wasUp) {
    wasUp = up;
    Serial.println(up ? "Blynk: connected" : "Blynk: lost, will retry");
  }
  if (!up) {
    if (s == WL_CONNECTED && millis() - tRetry > 30000UL) {
      tRetry = millis();
#if WDT_ENABLED
      esp_task_wdt_reset();
#endif
      Blynk.connect(3000);
    }
    return;
  }

  Blynk.run();

  static unsigned long tFc = 0;
  if (millis() - tFc > 60000UL) {
    tFc = millis();
    Blynk.virtualWrite(V0, forecast_mm_x10() / 10.0);
  }
#endif
}

void net_publish(const NodeReport& rep, BlockageVerdict verdict) {
  lastSeen[rep.node_id] = millis();
  if (offline[rep.node_id]) {
    offline[rep.node_id] = false;
    Serial.printf("N%d back online\n", rep.node_id);
    sdlog_event("online", "node recovered");
#if WIFI_MODE != 1
    if (Blynk.connected())
      Blynk.logEvent("node_offline", String("Node ") + rep.node_id + " recovered");
#endif
  }

#if WIFI_MODE != 1
  if (!Blynk.connected()) return;

  int base = rep.node_id * 10;
  Blynk.virtualWrite(base + 0, rep.level_mm);
  Blynk.virtualWrite(base + 1, rep.rate);
  Blynk.virtualWrite(base + 2, rep.severity);
  Blynk.virtualWrite(base + 3, rep.battery);
  Blynk.virtualWrite(base + 4, rep.trend);
  Blynk.virtualWrite(base + 5, rep.blockage ? 1 : 0);
  Blynk.virtualWrite(base + 6, (int)verdict);

  // Alerts fire on ESCALATION only. One per packet during an event would put a
  // notification on somebody's phone every five seconds, and the first thing
  // they would do is turn notifications off. Severity and the blockage flag are
  // tested separately because a drain can block without the water ever reaching
  // the unsafe depth.
  uint8_t prev = lastStatus[rep.node_id];
  uint8_t prevSev = (uint8_t)(prev & 0x0F);
  if (rep.severity >= GW_SEV_WATERLOGGED && prevSev < GW_SEV_WATERLOGGED)
    Blynk.logEvent("waterlogging", String("Node ") + rep.node_id + " waterlogging");

  // Fires on the gateway's VERDICT, not the node's measurement. A storm is not
  // a maintenance callout.
  static BlockageVerdict lastVerdict[NUM_NODES + 1] = {BLK_NONE};
  if (verdict > lastVerdict[rep.node_id] && verdict >= BLK_SUSPECTED) {
    Blynk.logEvent("blockage", String("Node ") + rep.node_id + " blockage " +
                   blockage_verdict_name(verdict));
  }
  lastVerdict[rep.node_id] = verdict;
  lastStatus[rep.node_id] = rep.status;
#endif
}

void net_check_offline() {
  for (int i = 1; i <= NUM_NODES; i++) {
    if (!offline[i] && lastSeen[i] != 0 && millis() - lastSeen[i] > NODE_TIMEOUT_MS) {
      offline[i] = true;
      // a silent node must stop voting in the storm test
      blockage_note_offline(i);
      Serial.printf("N%d OFFLINE\n", i);
      char d[32];
      int wrote = snprintf(d, sizeof d, "node %d silent", i);
      if (wrote < 0 || (size_t)wrote >= sizeof d) d[sizeof d - 1] = '\0';
      sdlog_event("offline", d);
#if WIFI_MODE != 1
      if (Blynk.connected())
        Blynk.logEvent("node_offline", String("Node ") + i + " has gone silent");
#endif
    }
  }
}
