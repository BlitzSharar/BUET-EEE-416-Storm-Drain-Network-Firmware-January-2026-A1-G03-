#ifndef ABS_CLOCK_H
#define ABS_CLOCK_H
#include <stdint.h>

// Absolute millisecond clock that survives deep sleep.
//
// Why this exists: both millis() and esp_timer_get_time() restart from zero
// when the ESP32 wakes from deep sleep. The Espressif documentation states this
// explicitly for esp_timer ("upon wakeup from deep sleep, the initialization
// timer restarts from zero"), and deep sleep is a full restart of the sketch.
// Our recession window can span many sleep cycles, so it cannot be timed on
// either of them.
//
// The fix is an accumulator held in RTC memory, which does survive deep sleep.
// Call clock_note_sleep(ms) immediately before esp_deep_sleep_start() and the
// accumulator absorbs both the elapsed wake time and the planned sleep time.
void     clock_begin();                // call once, before the first clock read
uint64_t clock_now_ms();               // ms since first power on, across sleeps
void     clock_note_sleep(uint32_t sleep_ms);
#endif
