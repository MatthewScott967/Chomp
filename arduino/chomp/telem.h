#ifndef TELEM_H
#define TELEM_H
#include "autofire.h"

// Forward decls
struct Detection;

bool sendSensorTelem(uint32_t loop_speed, int16_t pressure, uint16_t angle);
bool sendLeddarTelem(Detection* detections, unsigned int count, LeddarState state);

#endif //TELEM_H
