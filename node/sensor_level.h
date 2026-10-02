#ifndef SENSOR_LEVEL_H
#define SENSOR_LEVEL_H
#include <stdint.h>
void level_init();
int  level_read_mm();
int  level_last_mm();
int  level_rate_mm_min();
uint8_t level_last_pings();   // how many pings the adaptive filter needed

// False until the sensor has returned at least one valid echo since the last
// true power cycle. node.ino refuses to run the rule engine or transmit while
// this is false: an unmeasured level fed to the engine is indistinguishable
// from a measured one, and the engine will act on it.
bool level_have_reading();

// True only when the most recent level_read_mm() call obtained enough valid
// echoes. This differs from level_have_reading(), which means "ever valid".
bool level_last_sample_valid();
#endif
