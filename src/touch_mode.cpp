#include "touch_mode.h"
#include "config.h"
#include "display_mode.h"
#include <BleMouse.h>
#include <Arduino.h>
#include <Wire.h>

extern BleMouse BleMouseDevice;
extern SystemConfig g_cfg;

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
static bool isDragging = false; 

static unsigned long dualTouchStartTime = 0;
static uint8_t maxPointsInCurrentGesture = 0;
static unsigned long lastTouchMouseSendTime = 0;

static bool isDualFingerMoving = false; 

static int16_t dualStartP1Y = 0;          
static int16_t dualStartP2Y = 0;          
static unsigned long lastAutoScrollTime = 0; 

#define SLIDE_THRESHOLD     10   
#define TIMEOUT_WINDOW      250  
#define ZOOM_THRESHOLD      6    
#define COOLING_DOWN_MS     150  

#define AUTO_SCROLL_THRESHOLD  20  
#define AUTO_SCROLL_INTERVAL   40  

void initTouchMode() {
    // 注：GPIO 0/1 原为 EC11 编码器 A/B 相，旋钮硬件已拆除，此处不再占用该引脚

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
    isDragging = false; 
    maxPointsInCurrentGesture = 0;
    lastTouchMouseSendTime = 0;
    isDualFingerMoving = false;
    lastAutoScrollTime = 0;

    Serial.println("[M2] FT6336U 触控板驱动就绪。");
}

void updateTouchMode() {
    if (!BleMouseDevice.isConnected()) return;

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

                        if (hasMoved && (abs(deltaX) > g_cfg.deadzone_px || abs(deltaY) > g_cfg.deadzone_px)) {
                            int16_t move_x = (int16_t)(deltaX * g_cfg.touch_gain);
                            int16_t move_y = (int16_t)(deltaY * g_cfg.touch_gain);
                            
                            if (millis() - lastTouchMouseSendTime >= 12) {
                                lastTouchMouseSendTime = millis();
                                BleMouseDevice.move(move_x, move_y, 0);
                                lastTouchX = p1x;
                                lastTouchY = p1y;
                            }
                        }
                    }
                }
                // B. 双指不松手滑动滚动
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
                        else {
                            int16_t total_offset_p1 = p1y - dualStartP1Y;
                            int16_t total_offset_p2 = p2y - dualStartP2Y;
                            int16_t current_total_offset = (total_offset_p1 + total_offset_p2) / 2;

                            if (abs(current_total_offset) >= AUTO_SCROLL_THRESHOLD) {
                                isDualFingerMoving = true; 

                                unsigned long current_time = millis();
                                if (current_time - lastAutoScrollTime >= AUTO_SCROLL_INTERVAL) {
                                    lastAutoScrollTime = current_time;

                                    int8_t scroll_dir = (current_total_offset > 0) ? -1 : 1;
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
        
        // C. 手指抬起判定
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