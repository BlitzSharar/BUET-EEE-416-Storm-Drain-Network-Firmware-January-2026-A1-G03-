#ifndef UI_LOCAL_H
#define UI_LOCAL_H
#include "lora_rx.h"
void ui_init();
#include "blockage.h"
void ui_show_last(const NodeReport& rep, BlockageVerdict verdict);
#endif
