#ifndef MOUSE_MODE_H
#define MOUSE_MODE_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

void initMouseMode(); // Initialize the mouse mode
void updateMouseMode(); // Update the mouse mode

#ifdef __cplusplus
}
#endif

#endif // MOUSE_MODE_H