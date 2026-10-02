#ifndef CSV_FIELD_H
#define CSV_FIELD_H
#include <stddef.h>

// EVERY free-text field GOES THROUGH THIS.
//
// A comma inside an unquoted CSV field adds a column, and pandas does not
// raise: with one extra field it silently treats the first column as an index
// and shifts every subsequent column one place left FOR THE WHOLE FILE. Node
// IDs read as sequence numbers, battery reads as RSSI, and the verdict column
// holds the reason text. Nothing about the file looks wrong.
//
// The boot event did exactly this. Its detail is "gateway started, reset reason
// N" and it is normally the first record after the header on a fresh card, so
// every archive this project has written since is shifted. The blockage reason
// strings were hand-written comma-free to avoid it, which worked right up until
// somebody added a string without knowing that rule existed.
//
// So the rule is enforced here instead of being remembered: commas become
// semicolons, newlines and carriage returns become spaces, and the result is
// always exactly one field.
inline const char* csv_field(const char* in, char* out, size_t n) {
  // n == 0 leaves nowhere to put a terminator, so there is nothing to do but
  // return. The null-input path already guarded this and the main path did not,
  // which wrote one byte past the end for a zero length buffer. No caller does
  // that today; the guard is here because the next one might.
  if (n == 0) return out;
  if (!in) { out[0] = '\0'; return out; }
  size_t i = 0;
  for (; in[i] && i + 1 < n; i++) {
    char c = in[i];
    if (c == ',')                        out[i] = ';';
    else if (c == '\n' || c == '\r')     out[i] = ' ';
    else if (c == '"')                   out[i] = '\'';
    else                                 out[i] = c;
  }
  out[i] = '\0';
  return out;
}

#endif
