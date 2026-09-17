#ifndef TOUCH_MODE_H
#define TOUCH_MODE_H

#include <Arduino.h>

// 初始化触控与编码器模式
void initTouchMode();

// 高频刷新触控板与编码器状态
void updateTouchMode();


extern float sensitivityList[4];
extern uint8_t currentSensIdx;

#endif