#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include "config.h"
#include "watchdog.h"

// esp_task_wdt_init() changed signature between board packages. ESP-IDF 4,
// which is arduino-esp32 2.x, takes (timeout_seconds, panic). ESP-IDF 5, which
// is arduino-esp32 3.x, takes a config struct, and the Arduino framework has
// usually initialised the timer already, so init returns ESP_ERR_INVALID_STATE
// and reconfigure is the correct call. Writing it once here means the firmware
// compiles on whichever core the team happens to install.
static bool wdt_init_compat(uint32_t timeout_s, bool panic) {
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms     = timeout_s * 1000U;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic  = panic;
  esp_err_t err = esp_task_wdt_init(&cfg);
  if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_reconfigure(&cfg);
  return err == ESP_OK;
#else
  return esp_task_wdt_init(timeout_s, panic) == ESP_OK;
#endif
}

// Survives a watchdog reset (which is a restart, not a power cycle), so the
// node can report that it recovered rather than silently rebooting.
//
// RTC_NOINIT_ATTR, NOT RTC_DATA_ATTR, and the distinction is the whole point.
// RTC_DATA_ATTR lives in .rtc.data, which the bootloader reloads from the app
// image on every boot except a deep-sleep wake, so this counter was restored to
// its zero initialiser by the very reset it exists to count. It could never
// read higher than 1, and "reset count 1" after the fourth watchdog reset in an
// hour is worse than no counter at all: it says the problem is not recurring.
// .rtc.noinit is left alone by the bootloader and survives a watchdog, a panic,
// a brownout and a software restart alike.
//
// The price is that .rtc.noinit has no initialiser and holds whatever the RTC
// domain powered up with, so it needs a validity check of its own rather than
// trusting a declaration. Hence the magic word.
#define RESET_HISTORY_MAGIC 0x5A5EC0DEu
RTC_NOINIT_ATTR static uint32_t resetMagic;
RTC_NOINIT_ATTR static uint32_t resetCount;
static esp_reset_reason_t bootReason = ESP_RST_UNKNOWN;
static bool armed = false;

void wdt_begin() {
  bootReason = esp_reset_reason();

  // Establish the counter before reading it. A true power cycle clears the
  // history by design, and an unrecognised magic word means this is the first
  // boot after a flash or that the RTC domain lost power, where the retained
  // bytes are meaningless.
  if (bootReason == ESP_RST_POWERON || resetMagic != RESET_HISTORY_MAGIC) {
    resetMagic = RESET_HISTORY_MAGIC;
    resetCount = 0;
  }
  if (bootReason == ESP_RST_TASK_WDT || bootReason == ESP_RST_WDT ||
      bootReason == ESP_RST_INT_WDT) {
    resetCount++;
  }
#if WDT_ENABLED
  if (!wdt_init_compat(WDT_TIMEOUT_S, true)) {
    Serial.println("*** watchdog init failed; continuing without task watchdog ***");
    armed = false;
    return;
  }
  esp_err_t addResult = esp_task_wdt_add(NULL); // watch the Arduino loop task
  // Some Arduino cores subscribe the loop task themselves. In that case the
  // task is already protected and reset/delete calls are still valid.
  armed = (addResult == ESP_OK || addResult == ESP_ERR_INVALID_ARG);
  if (!armed) {
    Serial.printf("*** watchdog task subscription failed: %d ***\n", (int)addResult);
  }
#endif
}

void wdt_work_completed() {
#if WDT_ENABLED
  if (armed) esp_task_wdt_reset();
#endif
}

void wdt_idle_alive() {
#if WDT_ENABLED
  if (armed) esp_task_wdt_reset();
#endif
}

void wdt_disarm() {
#if WDT_ENABLED
  if (armed) { esp_task_wdt_delete(NULL); armed = false; }
#endif
}

const char* wdt_last_reset_reason() {
  switch (bootReason) {
    case ESP_RST_POWERON:   return "power on";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "panic or exception";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "other watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT:  return "brownout";
    default:                return "unknown";
  }
}

bool wdt_was_watchdog_reset() {
  return bootReason == ESP_RST_TASK_WDT || bootReason == ESP_RST_WDT ||
         bootReason == ESP_RST_INT_WDT;
}

uint32_t wdt_reset_count() { return resetCount; }
