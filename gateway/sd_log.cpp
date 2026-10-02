#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include "config.h"
#include "sd_log.h"
#include "csv_field.h"
#include "sd_schema.h"

static bool ready = false;
static File f;
static int sinceFlush = 0;

// A dedicated SPI bus instance for the card. The ESP32 has two usable hardware
// SPI peripherals; the radio keeps VSPI and the card gets HSPI, so neither
// library can disturb the other's bus settings.
static SPIClass sdSPI(HSPI);

// Schema, one row per received packet. Everything needed to reconstruct the
// analysis later: identity, the measurement, the decision, and the link quality
// that tells us whether to trust the record.
// iso_time is appended rather than inserted so every existing column keeps its
// position and any analysis already written against the old schema still works.
// It is empty until NTP has set the clock, which needs WiFi; ms is always there
// as the fallback. Without a wall clock the archive could only be correlated
// with the logbook by hand, which is a week 6 problem created in week 2.
static const char* HEADER =
  "ms,node_id,seq,level_mm,rate_mm_min,rain,severity,trend,stalled,state,verdict,reason,battery_pct,rssi_dbm,snr_db,iso_time\n";

// Local time as YYYY-MM-DD HH:MM:SS, or an empty field if the clock is unset.
static void iso_now(char* out, size_t n) {
  time_t t = time(nullptr);
  struct tm tmv;
  if (t < 1700000000L || !localtime_r(&t, &tmv)) { out[0] = '\0'; return; }
  strftime(out, n, "%Y-%m-%d %H:%M:%S", &tmv);
}

void sdlog_init() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  // SD_SPI_HZ defaults to 4 MHz in config.h. The project runs on breadboard
  // with jumper leads, where 16 MHz is optimistic and produces intermittent
  // init failures that look like a bad card. Raise it once the wiring is on
  // something more solid, if you ever need the throughput, which you do not:
  // one 60 byte row every few seconds is nothing.
  if (!SD.begin(PIN_SD_CS, sdSPI, SD_SPI_HZ)) {
    Serial.println("SD init failed: logging to serial only");
    ready = false;
    return;
  }
  // CHECK THE HEADER ON THE CARD BEFORE APPENDING TO IT.
  //
  // This used to be a bare `if (!SD.exists(...))`, so the header was written
  // once and never looked at again. Correct across reboots, silently wrong
  // across a firmware change, which is the case that happens. See sd_schema.h
  // for the archive that came back with three column widths in it.
  bool fresh = !SD.exists(SD_LOG_PATH);
  if (!fresh) {
    char line[256];
    size_t k = 0;
    File probe = SD.open(SD_LOG_PATH, FILE_READ);
    if (probe) {
      while (probe.available() && k + 1 < sizeof line) {
        int c = probe.read();
        if (c < 0 || c == '\n') break;
        line[k++] = (char)c;
      }
      probe.close();
    }
    line[k] = '\0';
    if (!sd_header_matches(line, HEADER)) {
      // Rename rather than delete. The old rows are still a real capture and
      // they are perfectly readable once they are alone in a file.
      char old[32];
      int n = 1;
      for (; n < 100; n++) {
        sd_rotation_name(n, old, sizeof old);
        if (!SD.exists(old)) break;
      }
      if (n < 100 && SD.rename(SD_LOG_PATH, old)) {
        Serial.printf("archive schema changed. Old file kept as %s, "
                      "starting a new one.\n", old);
        fresh = true;
      } else {
        // Could not rename, so appending would produce the mixed file this
        // check exists to prevent. Say so loudly and log to serial only.
        Serial.println("ARCHIVE SCHEMA MISMATCH and the old file could not be "
                       "renamed. Logging to serial only rather than writing a "
                       "file with two schemas in it. Move stormdrain.csv off "
                       "the card and reboot.");
        ready = false;
        return;
      }
    }
  }
  f = SD.open(SD_LOG_PATH, FILE_APPEND);
  if (!f) { ready = false; return; }
  if (fresh) f.print(HEADER);
  ready = true;
  Serial.println("SD log ready");
}

bool sdlog_ok() { return ready; }

// The CSV field sanitiser lives in csv_field.h so tests_host/csv_test.cpp can
// exercise it without an SD card. See that header for why it exists.


void sdlog_report(const NodeReport& rep, BlockageVerdict verdict, const char* reason) {
  if (!ready) {
    // The init message promises "logging to serial only", so deliver it. This
    // used to return in silence, which made a failed card indistinguishable on
    // the monitor from a hung gateway.
    Serial.printf("NOSD,%u,%u,%d,%d,%u,%u,%u,%u\n", rep.node_id, rep.seq,
                  rep.level_mm, (int)rep.rate, rep.rain, rep.severity,
                  rep.trend, rep.blockage ? 1u : 0u);
    return;
  }
  // A write can fail silently if the card is pulled, fills up, or corrupts.
  // Without this check the gateway would keep believing it was archiving while
  // writing to nothing, which is the worst possible failure for the one file
  // the whole recession analysis depends on.
  if (!f) {
    Serial.println("SD write handle lost, attempting reopen");
    f = SD.open(SD_LOG_PATH, FILE_APPEND);
    if (!f) { ready = false; Serial.println("SD archive FAILED, logging to serial only"); return; }
  }
  char ts[24]; iso_now(ts, sizeof ts);
  // EVERY free-text column, not only the ones that look risky today. The label
  // and the verdict name are closed enum-to-string tables with no commas in
  // them, so routing them through costs nothing and removes the question. The
  // stated rule and the enforced rule should be the same rule.
  char why[80];  csv_field(reason, why, sizeof why);
  char lbl[24];  csv_field(gw_label(rep.severity, rep.trend), lbl, sizeof lbl);
  char vrd[16];  csv_field(blockage_verdict_name(verdict), vrd, sizeof vrd);
  f.printf("%lu,%u,%u,%d,%d,%u,%u,%u,%u,%s,%s,%s,%u,%d,%.1f,%s\n",
           (unsigned long)rep.at_ms, rep.node_id, rep.seq, rep.level_mm,
           (int)rep.rate, rep.rain, rep.severity, rep.trend,
           rep.blockage ? 1u : 0u, lbl, vrd, why,
           rep.battery, rep.rssi, rep.snr, ts);
  if (++sinceFlush >= SD_FLUSH_EVERY) sdlog_flush();
}

void sdlog_event(const char* kind, const char* detail) {
  if (!ready) {
    // Same promise, kept for events too.
    Serial.printf("NOSD,EVENT,%s: %s\n", kind ? kind : "", detail ? detail : "");
    return;
  }
  // Same reopen check as sdlog_report. An event row is exactly what you want
  // to survive, so it must not be the one write that goes nowhere unnoticed.
  if (!f) {
    Serial.println("SD write handle lost, attempting reopen");
    f = SD.open(SD_LOG_PATH, FILE_APPEND);
    if (!f) { ready = false; Serial.println("SD archive FAILED, logging to serial only"); return; }
  }
  // Padded to the full column count. This used to write four fields into a ten
  // column file, so pandas.read_csv either threw or silently misaligned every
  // row after it, in week 6, on the one file the whole analysis reads from.
  // node_id carries the literal EVENT so events are trivial to filter out:
  //   df[df.node_id != "EVENT"]
  char ts[24]; iso_now(ts, sizeof ts);
  // Padded to the FULL column count. The header is 16 wide:
  // ms,node_id,seq,level_mm,rate_mm_min,rain,severity,trend,stalled,state,
  // verdict,reason,battery_pct,rssi_dbm,snr_db,iso_time
  // The message goes in the last column so a short row can never shift it.
  // This drifted twice: it was 4 fields against 10, then 11 against 16, and
  // both times pandas NaN-padded silently and put event text in a data column.
  char k[24];  csv_field(kind,   k, sizeof k);
  char d[96];  csv_field(detail, d, sizeof d);
  f.printf("%lu,EVENT,,,,,,,,,,,,,,%s %s: %s\n", millis(), ts, k, d);
  sdlog_flush();
}

void sdlog_flush() { if (ready) { f.flush(); sinceFlush = 0; } }
