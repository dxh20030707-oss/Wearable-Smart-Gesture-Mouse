#include "touch_mode.h"
#include "config.h"
#include "display_mode.h"
#include <BleMouse.h>
#include <Arduino.h>
#include <Wire.h>

extern BleMouse BleMouseDevice;
extern float sensitivityList[];
extern uint8_t currentSensIdx;
extern uint8_t global_sens_percent;

int16_t oled_touch_x = 0;
int16_t oled_touch_y = 0;
int16_t oled_touch2_x = 0;     
int16_t oled_touch2_y = 0;     
uint8_t oled_touch_points = 0; 
uint8_t oled_click_count = 0;

#define FT6336U_I2C_ADDR    0x38  
#define FT_REG_TD_STATUS    0x02  
#define FT_REG_P1_XH        0x03  
#define FT_REG_P2_XH        0x09  

volatile int32_t encoderCount = 0;
static int32_t lastEncoderCount = 0;

extern bool is_ctrl_pressed; 

void IRAM_ATTR handleEncoderM2ISR() {
    int aState = digitalRead(PIN_M2_TIM_CH1); 
    int bState = digitalRead(PIN_M2_TIM_CH2); 
    if (aState == bState) encoderCount++; else encoderCount--;
}

volatile bool ft_interrupt_flag = false; 
void IRAM_ATTR handleTouchINT_ISR() {
    ft_interrupt_flag = true; 
}

static bool readFT6336U_Dual(int16_t &x1, int16_t &y1, int16_t &x2, int16_t &y2, uint8_t &points) {
    Wire.beginTransmission(FT6336U_I2C_ADDR);
    Wire.write(FT_REG_TD_STATUS);
    if (Wire.endTransmission() != 0) return false; 

    Wire.requestFrom(FT6336U_I2C_ADDR, 1);
    if (!Wire.available()) return false;
    points = Wire.read() & 0x0F; 

    if (points == 0) {
        x1 = 0; y1 = 0; x2 = 0; y2 = 0;
        return true; 
    }

    if (points == 1) {
        Wire.beginTransmission(FT6336U_I2C_ADDR);
        Wire.write(FT_REG_P1_XH); 
        if (Wire.endTransmission() != 0) return false;

        Wire.requestFrom(FT6336U_I2C_ADDR, 4);
        if (Wire.available() == 4) {
            uint8_t xh = Wire.read(); uint8_t xl = Wire.read();
            uint8_t yh = Wire.read(); uint8_t yl = Wire.read();
            x1 = ((xh & 0x0F) << 8) | xl;
            y1 = ((yh & 0x0F) << 8) | yl;
            x2 = 0; y2 = 0; 
            return true;
        }
    } 
    else if (points == 2) {
        Wire.beginTransmission(FT6336U_I2C_ADDR);
        Wire.write(FT_REG_P1_XH);
        if (Wire.endTransmission() != 0) return false;

        Wire.requestFrom(FT6336U_I2C_ADDR, 10);
        if (Wire.available() == 10) {
            uint8_t x1h = Wire.read(); uint8_t x1l = Wire.read();
            uint8_t y1h = Wire.read(); uint8_t y1l = Wire.read();
            x1 = ((x1h & 0x0F) << 8) | x1l;
            y1 = ((y1h & 0x0F) << 8) | y1l;

            Wire.read(); Wire.read(); 

            uint8_t x2h = Wire.read(); uint8_t x2l = Wire.read();
            uint8_t y2h = Wire.read(); uint8_t y2l = Wire.read(); 
            x2 = ((x2h & 0x0F) << 8) | x2l;
            y2 = ((y2h & 0x0F) << 8) | y2l;
            return true;
        }
    }
    return false;
}

static bool isTouching = false;
static uint8_t lastPoints = 0;
static int16_t startTouchX = 0, startTouchY = 0;
static int16_t lastTouchX = 0, lastTouchY = 0;
static int16_t lastP1Y = 0, lastP2Y = 0;
static uint8_t clickCount = 0;
static unsigned long touchStartTime = 0;
static unsigned long touchReleaseTime = 0;
static bool hasMoved = false;
static unsigned long dualTouchExitTime = 0;
static bool wasDualScrolling = false; 
static int16_t lastTouchDist = 0; 
static unsigned long lastKnobTime = 0;
static bool isDragging = false; 

static unsigned long dualTouchStartTime = 0;
static uint8_t maxPointsInCurrentGesture = 0;
static unsigned long lastTouchMouseSendTime = 0;

static bool isDualFingerMoving = false; 

// 双指不松手持续滚动组件
static int16_t dualStartP1Y = 0;          
static int16_t dualStartP2Y = 0;          
static unsigned long lastAutoScrollTime = 0; 

#define SLIDE_THRESHOLD     10   
#define TIMEOUT_WINDOW      250  
#define ZOOM_THRESHOLD      6    
#define COOLING_DOWN_MS     150  
#define KNOB_DEBOUNCE_MS    120  

// 持续滚动核心体验控制参数
#define AUTO_SCROLL_THRESHOLD  20  // 降低门槛，滑开20像素就激活持续不松手滚动
#define AUTO_SCROLL_INTERVAL   40  // 40ms发送一次滚轮包

void initTouchMode() {
    pinMode(PIN_M2_TIM_CH1, INPUT_PULLUP);
    pinMode(PIN_M2_TIM_CH2, INPUT_PULLUP);
    encoderCount = 0;
    lastEncoderCount = 0;
    attachInterrupt(digitalPinToInterrupt(PIN_M2_TIM_CH1), handleEncoderM2ISR, CHANGE);

    pinMode(PIN_M2_RST, OUTPUT);
    digitalWrite(PIN_M2_RST, HIGH); 
    delay(10);
    digitalWrite(PIN_M2_RST, LOW);  
    delay(20);
    digitalWrite(PIN_M2_RST, HIGH); 
    delay(200); 
    
    pinMode(PIN_M2_INT, INPUT_PULLUP);
    ft_interrupt_flag = false;
    attachInterrupt(digitalPinToInterrupt(PIN_M2_INT), handleTouchINT_ISR, CHANGE);

    wasDualScrolling = false;
    dualTouchExitTime = 0;
    lastTouchDist = 0;
    lastKnobTime = 0;
    isDragging = false; 
    maxPointsInCurrentGesture = 0;
    lastTouchMouseSendTime = 0;
    isDualFingerMoving = false;
    lastAutoScrollTime = 0;

    Serial.println("[M2] FT6336U 真实总线通道打通，自适应手势层已就绪。");
}

void updateTouchMode() {
    if (!BleMouseDevice.isConnected()) return;

    if (encoderCount != lastEncoderCount) {
        int32_t diff = encoderCount - lastEncoderCount;
        lastEncoderCount = encoderCount;
        
        if (millis() - lastKnobTime > KNOB_DEBOUNCE_MS) {
            lastKnobTime = millis();
            int8_t scroll_pulse = (diff > 0) ? 1 : -1;
            
            if (is_ctrl_pressed) {
                BleMouseDevice.move(0, 0, scroll_pulse);
            } 
            else {
                if (diff > 0) {
                    if (global_sens_percent < 100) global_sens_percent += 5;
                } else {
                    if (global_sens_percent > 5) global_sens_percent -= 5;
                }
                
                if (global_sens_percent <= 25) currentSensIdx = 0;
                else if (global_sens_percent <= 50) currentSensIdx = 1;
                else if (global_sens_percent <= 75) currentSensIdx = 2;
                else currentSensIdx = 3;

                extern unsigned long lastOledRefreshTime;
                lastOledRefreshTime = 0;
            }
        }
    }

    int16_t p1x = 0, p1y = 0, p2x = 0, p2y = 0;
    uint8_t touch_points = 0;

    if (ft_interrupt_flag || isTouching) {
        ft_interrupt_flag = false; 

        bool read_success = readFT6336U_Dual(p1x, p1y, p2x, p2y, touch_points);
        
        if (read_success) {
            uint8_t actual_physical_points = touch_points;

            if (touch_points > 0 && actual_physical_points > 0) {
                oled_touch_points = touch_points;
                oled_touch_x = p1x; oled_touch_y = p1y;
                oled_touch2_x = p2x; oled_touch2_y = p2y;
            }

            float real_sens_multiplier = sensitivityList[currentSensIdx] * 1.2f;

            if (touch_points > maxPointsInCurrentGesture) {
                maxPointsInCurrentGesture = touch_points;
            }

            if (!isTouching && touch_points > 0 && actual_physical_points > 0) {
                isTouching = true;
                touchStartTime = millis();
                hasMoved = false;
                isDualFingerMoving = false; 
                startTouchX = p1x; startTouchY = p1y;
                lastTouchX = p1x;  lastTouchY = p1y;   
                lastP1Y = p1y;     lastP2Y = p2y;
                lastPoints = touch_points;
                maxPointsInCurrentGesture = touch_points;
                
                if (touch_points == 2) {
                    dualTouchStartTime = millis();
                    dualStartP1Y = p1y; 
                    dualStartP2Y = p2y;
                    lastAutoScrollTime = millis();
                }

                if (clickCount == 1 && (touchStartTime - touchReleaseTime < TIMEOUT_WINDOW)) {
                } else {
                    clickCount = 0;
                }
            } 
            else if (isTouching && touch_points > 0) {
                if (lastPoints != 2 && touch_points == 2) {
                    dualTouchStartTime = millis();
                    if (actual_physical_points == 2) {
                        dualStartP1Y = p1y; 
                        dualStartP2Y = p2y;
                        lastP1Y = p1y; lastP2Y = p2y;
                    }
                    lastAutoScrollTime = millis();
                }
                if (lastPoints == 2 && touch_points == 1) {
                    dualTouchExitTime = millis(); 
                    wasDualScrolling = true;      
                }

                // A. 单指位移控光标
                if (touch_points == 1) {
                    if (wasDualScrolling && (millis() - dualTouchExitTime < COOLING_DOWN_MS)) {
                        if (actual_physical_points > 0) {
                            lastTouchX = p1x; lastTouchY = p1y;
                        }
                    } 
                    else if (lastPoints == 1 && actual_physical_points > 0) {
                        int16_t deltaX = p1x - lastTouchX;
                        int16_t deltaY = p1y - lastTouchY; 

                        if (!hasMoved && (abs(p1x - startTouchX) > SLIDE_THRESHOLD || abs(p1y - startTouchY) > SLIDE_THRESHOLD)) {
                            hasMoved = true;
                            if (clickCount == 1 && (p1y - startTouchY) > SLIDE_THRESHOLD) {
                                isDragging = true; 
                                BleMouseDevice.press(MOUSE_LEFT); 
                                clickCount = 0; 
                            }
                        }

                        if (hasMoved && (abs(deltaX) > 1 || abs(deltaY) > 1)) {
                            int16_t move_x = (int16_t)(deltaX * real_sens_multiplier);
                            int16_t move_y = (int16_t)(deltaY * real_sens_multiplier);
                            
                            if (millis() - lastTouchMouseSendTime >= 12) {
                                lastTouchMouseSendTime = millis();
                                BleMouseDevice.move(move_x, move_y, 0);
                                lastTouchX = p1x;
                                lastTouchY = p1y;
                            }
                        }
                    }
                }
                // B. 双指无损持续滚动核心逻辑
                else if (touch_points == 2) {
                    if (lastPoints != 2) {
                        if (actual_physical_points == 2) {
                            dualStartP1Y = p1y;
                            dualStartP2Y = p2y;
                            lastP1Y = p1y; lastP2Y = p2y;
                            lastTouchDist = sqrt(pow(p1x - p2x, 2) + pow(p1y - p2y, 2));
                        }
                        hasMoved = true; wasDualScrolling = true;
                        lastAutoScrollTime = millis();
                    }

                    if (actual_physical_points == 2) {
                        int16_t currentDist = sqrt(pow(p1x - p2x, 2) + pow(p1y - p2y, 2));
                        int16_t distDelta = currentDist - lastTouchDist;

                        if (abs(distDelta) >= ZOOM_THRESHOLD) {
                            isDualFingerMoving = true; 
                            if (millis() - lastTouchMouseSendTime >= 40) {
                                lastTouchMouseSendTime = millis();
                                BleMouseDevice.press(MOUSE_MIDDLE); 
                                if (distDelta > 0) BleMouseDevice.move(0, 0, 1); 
                                else BleMouseDevice.move(0, 0, -1);
                                delay(5);
                                BleMouseDevice.release(MOUSE_MIDDLE); 
                            }
                            lastTouchDist = currentDist;
                            lastP1Y = p1y; lastP2Y = p2y;
                        } 
                        // 🎯 【持续滚动引擎补全修复】
                        else {
                            int16_t total_offset_p1 = p1y - dualStartP1Y;
                            int16_t total_offset_p2 = p2y - dualStartP2Y;
                            int16_t current_total_offset = (total_offset_p1 + total_offset_p2) / 2;

                            if (abs(current_total_offset) >= AUTO_SCROLL_THRESHOLD) {
                                isDualFingerMoving = true; 

                                unsigned long current_time = millis();
                                if (current_time - lastAutoScrollTime >= AUTO_SCROLL_INTERVAL) {
                                    lastAutoScrollTime = current_time;

                                    // 🎯 精准隔离分流：
                                    // 1. 手指从上往下滑（current_total_offset 为正数）：向下滚动页面
                                    // 2. 手指从下往上划（current_total_offset 为负数）：向上滚动页面
                                    int8_t scroll_dir = 0;
                                    if (current_total_offset > 0) {
                                        scroll_dir = -1; // 👈 补全向下滚轮信号
                                    } else {
                                        scroll_dir = 1;  // 👈 保持向上滚轮信号
                                    }
                                    
                                    BleMouseDevice.move(0, 0, scroll_dir);
                                }
                            }
                        }
                        lastTouchDist = currentDist;
                    }
                }
                
                if (actual_physical_points > 0) {
                    lastPoints = touch_points;
                }
            }
        }
        
        // C. 手指完全抬起释放时刻
        if (isTouching && touch_points == 0) {
            isTouching = false;
            touchReleaseTime = millis();
            oled_touch_points = 0; 
            
            if (isDragging) {
                BleMouseDevice.release(MOUSE_LEFT); 
                isDragging = false;
                hasMoved = true;
            }
            if (wasDualScrolling) hasMoved = true; 
            
            if (maxPointsInCurrentGesture == 2) {
                if ((touchReleaseTime - dualTouchStartTime < 300) && (!isDualFingerMoving)) {
                    BleMouseDevice.click(MOUSE_RIGHT);
                    clickCount = 0; 
                    hasMoved = true; 
                }
            }

            if (!hasMoved && (touchReleaseTime - touchStartTime < TIMEOUT_WINDOW)) {
                clickCount++;
            }
            oled_click_count = clickCount;
            wasDualScrolling = false; 
            isDualFingerMoving = false; 
            maxPointsInCurrentGesture = 0;
        }
    }

    if (clickCount > 0 && !isTouching && (millis() - touchReleaseTime > TIMEOUT_WINDOW)) {
        switch (clickCount) {
            case 1: BleMouseDevice.click(MOUSE_LEFT); break;
            case 2: BleMouseDevice.click(MOUSE_LEFT); BleMouseDevice.click(MOUSE_LEFT); break;
        }
        clickCount = 0; oled_click_count = 0; 
    }
}