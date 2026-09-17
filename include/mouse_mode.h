#ifndef MOUSE_MODE_H
#define MOUSE_MODE_H

// C 语言环境引入标准布尔支持，C++ 环境自动识别
#ifdef __cplusplus
#include <Arduino.h>
#else
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void initMouseMode();
void updateMouseMode();
bool isMouseConnected();
float getMouseSensitivity();

#ifdef __cplusplus
}
#endif

#endif // MOUSE_MODE_H