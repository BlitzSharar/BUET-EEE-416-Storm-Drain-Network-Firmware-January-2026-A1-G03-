#ifndef SENSOR_RAIN_H
#define SENSOR_RAIN_H
#include <stdint.h>
struct RainInput { bool raining; uint8_t mm_x10; bool fromBroadcast; };
void rain_init();
RainInput rain_get();                     // local plate if fitted, else gateway broadcast
void rain_set_broadcast(bool raining, uint8_t mm_x10);  // called by lora_rx

// EVERY NODE CARRIES A PLATE NOW, so this is true on all three and the
// broadcast path below is a fallback for a NODE_HAS_RAIN 0 build only.
//
// It used to be node 1 only, and the consequence was the worst failure mode
// this project had: nodes 2 and 3 took rainfall from a gateway broadcast, their
// blockage rule depended on that broadcast arriving, and when it did not the
// rule was silently disabled with nothing to say so.
//
// Note what the node does NOT do with its plate. The rainfall never enters the
// recession rule. The node reports the plate reading as its own field and the
// gateway combines rainfall across all three sites. See gateway/blockage.h.
bool rain_context_valid();

// The raw plate reading from the last rain_get(), for the bring-up line and for
// calibration. Nothing decides on it; it exists so a human can see it.
int rain_last_adc();
#endif
