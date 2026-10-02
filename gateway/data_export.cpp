#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"
#include "sd_log.h"
#include "data_export.h"
#if WDT_ENABLED
  #include <esp_task_wdt.h>
#endif

static WebServer server(80);
static bool up = false;

// ---------------------------------------------------------------------------
// Serving a file blocks the loop, which means packets arriving during a
// download are missed. The archive is small, a few hundred kB after days of
// running, so a download is a second or two. That is acceptable, but it is a
// real cost and it is why the file is not served automatically or on a timer.
// Download between trials, not during one.
// ---------------------------------------------------------------------------

static void feed() {
#if WDT_ENABLED
  esp_task_wdt_reset();     // a long download must not trip the watchdog
#endif
}

static void handleIndex() {
  // Flush before measuring. The log only flushes every SD_FLUSH_EVERY records,
  // so without this the file on the card is still empty and the page reports
  // no archive even though the header and several rows have been written.
  sdlog_flush();

  uint32_t bytes = 0, rows = 0;
  File f = SD.open(SD_LOG_PATH, FILE_READ);
  if (f) {
    bytes = f.size();
    while (f.available()) if (f.read() == '\n') rows++;
    if (rows) rows--;                 // the header line is not a record
    f.close();
  }

  String h = F("<!doctype html><meta name=viewport content='width=device-width'>"
               "<style>body{font:16px system-ui;margin:2rem;max-width:34rem}"
               "a{display:inline-block;padding:.6rem 1rem;background:#12315e;color:#fff;"
               "text-decoration:none;border-radius:4px}code{background:#eee;padding:2px 5px}</style>"
               "<h2>Storm-drain gateway</h2>");
  if (bytes && rows) {
    h += "<p><b>" + String(rows) + "</b> packets logged, " + String(bytes) + " bytes.</p>";
    h += F("<p><a href='/stormdrain.csv'>Download the CSV</a></p>");
    h += F("<p style='color:#666;font-size:14px'>The gateway stops receiving packets "
           "while this downloads. Do it between trials, not during one.</p>");
  } else if (bytes) {
    h += F("<p>The archive exists but holds no packets yet, only the column header.</p>"
           "<p style='color:#666;font-size:14px'>Power up a node and refresh. "
           "The gateway prints a line for every packet it receives.</p>");
    h += F("<p><a href='/stormdrain.csv'>Download it anyway</a></p>");
  } else {
    h += F("<p>No archive on the card.</p>"
           "<p style='color:#666;font-size:14px'>If the serial monitor said "
           "<code>SD log ready</code> at boot, the file will appear once the first "
           "packet arrives. If it said <code>SD init failed</code>, check the wiring "
           "on GPIO25, 27, 32 and 33.</p>");
  }
  h += "<p style='color:#666;font-size:14px'>Refresh this page to update the count.</p>";
  server.send(200, "text/html", h);
}

static void handleCsv() {
  // Flush first. The log flushes every SD_FLUSH_EVERY records, so without this
  // the most recent rows are still in the buffer and would not be in the file
  // you just downloaded. That is exactly the data you most want.
  sdlog_flush();
  feed();

  File f = SD.open(SD_LOG_PATH, FILE_READ);
  if (!f) { server.send(404, "text/plain", "no archive on the card"); return; }

  server.sendHeader("Content-Disposition", "attachment; filename=stormdrain.csv");
  server.setContentLength(f.size());
  server.send(200, "text/csv", "");

  uint8_t buf[512];
  while (f.available()) {
    size_t n = f.read(buf, sizeof buf);
    server.client().write(buf, n);
    feed();
  }
  f.close();
  Serial.println("archive downloaded over WiFi");
}

// ---------------------------------------------------------------------------
// Serial fallback. Works with no network at all. Type d and press enter.
// ---------------------------------------------------------------------------
static void dumpSerial() {
  sdlog_flush();
  File f = SD.open(SD_LOG_PATH, FILE_READ);
  if (!f) { Serial.println("no archive on the card"); return; }

  Serial.println("---- BEGIN stormdrain.csv ----");
  while (f.available()) { Serial.write(f.read()); feed(); }
  f.close();
  Serial.println("---- END stormdrain.csv ----");
  Serial.println("Select from BEGIN to END, copy, and save as stormdrain.csv");
}

void dexport_begin() {
  server.on("/", handleIndex);
  server.on("/stormdrain.csv", handleCsv);
  server.begin();
  up = true;
  Serial.println("data export ready. Type 'd' for a serial dump.");
}

void dexport_poll() {
  if (!up) return;

  // Print the address once WiFi actually associates, not before.
#if WIFI_MODE != 1
  // Announce the station address once it exists. In dual mode the access point
  // address was already printed at boot and does not change, so this is the
  // only one worth repeating.
  static bool announced = false;
  if (!announced && WiFi.status() == WL_CONNECTED) {
    announced = true;
    Serial.print("archive also at http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
  }
#endif

  server.handleClient();

  if (Serial.available()) {
    int c = Serial.read();
    if (c == 'd' || c == 'D') dumpSerial();
  }
}
