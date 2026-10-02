#ifndef UI_LOCAL_H
#define UI_LOCAL_H
#include <stdint.h>
#include "status_engine.h"
void ui_init();
void ui_splash();
void ui_show(int level_mm, int rate, const EngineOut& st, uint8_t battery_pct);
void ui_set_power(bool on);
#endif
