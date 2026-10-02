#ifndef SD_SCHEMA_H
#define SD_SCHEMA_H
#include <stddef.h>
#include <stdio.h>

// THE ARCHIVE MUST NEVER HOLD TWO SCHEMAS AT ONCE.
//
// sdlog_init() wrote the column header only when the file did not already
// exist. That is correct for an append across reboots and silently wrong across
// a firmware change, which is the case that actually happens: the schema grew
// from 11 columns to 16, the card already had a file, so the old header stayed
// at the top and 392 new 16-column rows were appended underneath it.
//
// A real capture from 19 September contained THREE widths in one file. Eleven
// from the old build, sixteen from the new one, and twelve from the old event
// rows written before csv_field() existed, where the unescaped comma in
// "gateway started, reset reason 1" added a column of its own.
//
// pandas reads that file against an 11-column header. Every wider row is
// either dropped or shifted, and no exception is raised for the shifted ones.
// A whole evening of bench data needed hand surgery before it could be read.
//
// So the header is now CHECKED rather than assumed. If what is on the card
// disagrees with what this build writes, the old file is renamed and a fresh
// one is started. Renamed, never deleted: the old data is still somebody's
// evening, and it is readable on its own once it is in a file of its own.
//
// Both helpers are here rather than in sd_log.cpp so tests_host/sd_schema_test
// can exercise them without an SD card.

// Does the line read back from the card match the header this build writes?
// Trailing CR and LF are ignored on both sides, because the card may have been
// written by a build that used a different line ending, and that is not a
// schema difference. Everything else must match exactly: a renamed column is a
// schema change even when the width is unchanged, and a width-preserving rename
// is the one an operator is least likely to notice.
inline bool sd_header_matches(const char* stored, const char* expected) {
  if (!stored || !expected) return false;
  size_t i = 0, j = 0;
  for (;;) {
    while (stored[i]   == '\r' || stored[i]   == '\n') i++;
    while (expected[j] == '\r' || expected[j] == '\n') j++;
    if (stored[i] == '\0' || expected[j] == '\0') break;
    if (stored[i] != expected[j]) return false;
    i++; j++;
  }
  return stored[i] == '\0' && expected[j] == '\0';
}

// Where a superseded archive goes. Numbered rather than timestamped because the
// clock is not set at the point this runs: NTP needs WiFi, WiFi comes up after
// sdlog_init(), and a file called stormdrain_1970-01-01.csv helps nobody.
inline const char* sd_rotation_name(int n, char* out, size_t len) {
  if (!out || len == 0) return out;
  int wrote = snprintf(out, len, "/stormdrain_old%d.csv", n);
  if (wrote < 0 || (size_t)wrote >= len) out[len - 1] = '\0';
  return out;
}

#endif
