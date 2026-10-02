#ifndef LORA_BCAST_H
#define LORA_BCAST_H
#include <stdint.h>
void bcast_send(bool raining, uint16_t forecast_mm_x10);  // gateway to all nodes
#endif
