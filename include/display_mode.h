#ifndef DISPLAY_MODE_H
#define DISPLAY_MODE_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// 强力导出显式对象给其他模块
extern Adafruit_SSD1306 display;

// 统一参数类型为标准的 int16_t 与 uint8_t
void initDisplayMode(void);
void updateDisplayMode1(int16_t x, int16_t y, float sens, bool is_connected);
void updateDisplayMode2(int16_t x, int16_t y, float sens, bool is_connected);
void drawWheelConfigUI(bool is_downsliding, bool is_upsliding, float wheel_sens, bool is_connected);

#endif // DISPLAY_MODE_H