#include "mouse_mode.h"
#include "config.h"
#include "mpu6050_driver.h" 
#include <BleMouse.h>
#include <Arduino.h>
#include <Wire.h>

extern BleMouse BleMouseDevice;
extern uint8_t global_sens_percent; 
extern uint8_t global_wheel_sens_percent; 

int16_t oled_mouse_x = 0;
int16_t oled_mouse_y = 0;
bool is_airmouse_locked = false; 

int32_t gyro_offset_pitch = 0; 
int32_t gyro_offset_yaw = 0;   

const int16_t DEADZONE = 80;    
const int16_t DAMPING = 120;    
static float smooth_dx = 0;
static float smooth_dy = 0;
const float FILTER_ALPHA = 0.4f; 
static unsigned long lastBleTime = 0;

volatile int32_t mouseWheelCount = 0;
static int32_t lastMouseWheelCount = 0;
static unsigned long lastWheelSendTime = 0;

// ====================================================
// 🎯 极致精简中断：寄存器直读法
// ====================================================
void IRAM_ATTR handleEncoderM1ISR() {
    if (!is_airmouse_locked) {
        return;
    }

    static uint8_t encoder_state = 0;
    
    uint32_t gpio_in = REG_READ(GPIO_IN_REG); 
    
    uint8_t s = 0;
    if (gpio_in & (1 << PIN_M1_LEFT))  s |= 1;  
    if (gpio_in & (1 << PIN_M1_RIGHT)) s |= 2;  
    
    encoder_state = ((encoder_state << 2) | s) & 0x0F;
    
    if (encoder_state == 0x07) { 
        mouseWheelCount++;
    } else if (encoder_state == 0x0D) { 
        mouseWheelCount--;
    }
}

static void updateWheelLogic() {
    if (mouseWheelCount != lastMouseWheelCount) {
        int32_t diff = mouseWheelCount - lastMouseWheelCount;
        lastMouseWheelCount = mouseWheelCount;
        
        if (is_airmouse_locked) {
            if (millis() - lastWheelSendTime > 35) {
                lastWheelSendTime = millis();
                
                float multiplier = (float)global_wheel_sens_percent / 50.0f;
                int8_t scroll_steps = (diff > 0) ? 1 : -1;
                scroll_steps = (int8_t)(scroll_steps * multiplier);
                if (scroll_steps == 0) scroll_steps = (diff > 0) ? 1 : -1;

                BleMouseDevice.move(0, 0, scroll_steps); 
            }
        }
    }
}

static void updateNormalButtons() {
    static bool lastLeftState = HIGH;
    static bool lastRightState = HIGH;
    static unsigned long lastLeftDebounce = 0;
    static unsigned long lastRightDebounce = 0;
    
    bool currLeft = digitalRead(PIN_M1_LEFT);   
    bool currRight = digitalRead(PIN_M1_RIGHT); 
    
    if (!is_airmouse_locked) {
        if (currLeft != lastLeftState) {
            if (millis() - lastLeftDebounce > 25) { 
                lastLeftDebounce = millis();
                if (currLeft == LOW)  BleMouseDevice.press(MOUSE_LEFT);
                else                  BleMouseDevice.release(MOUSE_LEFT);
                lastLeftState = currLeft;
            }
        }

        if (currRight != lastRightState) {
            if (millis() - lastRightDebounce > 25) {
                lastRightDebounce = millis();
                if (currRight == LOW)  BleMouseDevice.press(MOUSE_RIGHT);
                else                   BleMouseDevice.release(MOUSE_RIGHT);
                lastRightState = currRight;
            }
        }
    } else {
        if (lastLeftState == LOW) { BleMouseDevice.release(MOUSE_LEFT); lastLeftState = HIGH; }
        if (lastRightState == LOW) { BleMouseDevice.release(MOUSE_RIGHT); lastRightState = HIGH; }
    }
}

static void updateLockKeyLogic() {
    static bool lastLockBtnState = HIGH;
    static unsigned long lockBtnDebounceTime = 0;
    bool currentLockState = digitalRead(PIN_M1_PAUSE); 
    
    if (currentLockState != lastLockBtnState) {
        if (millis() - lockBtnDebounceTime > 50) { 
            lockBtnDebounceTime = millis();
            if (currentLockState == LOW) { 
                is_airmouse_locked = !is_airmouse_locked;
                mouseWheelCount = 0;
                lastMouseWheelCount = 0;
                
                if (!is_airmouse_locked) {
                     BleMouseDevice.release(MOUSE_LEFT);
                     BleMouseDevice.release(MOUSE_RIGHT);
                }
            }
            lastLockBtnState = currentLockState;
        }
    }
}

static void updateSensKeyLogic() {
    static bool lastSensBtnState = HIGH;
    static unsigned long sensBtnPressTime = 0;
    static unsigned long lastAutoStepTime = 0;
    static bool isLongPressed = false;
    bool currentSensState = digitalRead(PIN_M1_SENS); 

    if (currentSensState == LOW && lastSensBtnState == HIGH) {
        sensBtnPressTime = millis();
        isLongPressed = false;
        delay(5); 
    }
    if (currentSensState == LOW) {
        if (millis() - sensBtnPressTime >= 400) { 
            isLongPressed = true;
            if (millis() - lastAutoStepTime >= 150) { 
                lastAutoStepTime = millis();
                if (!is_airmouse_locked) {
                    global_sens_percent += 10;
                    if (global_sens_percent > 100) global_sens_percent = 10;
                } else {
                    global_wheel_sens_percent += 10;
                    if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10;
                }
            }
        }
    }
    if (currentSensState == HIGH && lastSensBtnState == LOW) {
        if (!isLongPressed && (millis() - sensBtnPressTime > 25)) {
            if (!is_airmouse_locked) {
                global_sens_percent += 10;
                if (global_sens_percent > 100) global_sens_percent = 10;
            } else {
                global_wheel_sens_percent += 10;
                if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10;
            }
        }
    }
    lastSensBtnState = currentSensState;
}

void initMouseMode() {
    is_airmouse_locked = false;
    
    pinMode(PIN_M1_LEFT, INPUT_PULLUP);   
    pinMode(PIN_M1_RIGHT, INPUT_PULLUP);  
    pinMode(PIN_M1_PAUSE, INPUT_PULLUP);  
    pinMode(PIN_M1_SENS, INPUT_PULLUP);   
    
    mouseWheelCount = 0;
    lastMouseWheelCount = 0;
    
    attachInterrupt(digitalPinToInterrupt(PIN_M1_LEFT), handleEncoderM1ISR, CHANGE);

    mpu6050_init(); 
    delay(50);
    gyro_offset_pitch = 0; gyro_offset_yaw = 0;
    for (int i = 0; i < 50; i++) {
        mpu6050_raw_data_t temp_data;
        mpu6050_read_raw(&temp_data);
        int16_t *ptr = (int16_t *)&temp_data;
        gyro_offset_pitch += ptr[5]; gyro_offset_yaw += ptr[6];   
        delay(5);
    }
    gyro_offset_pitch /= 50; gyro_offset_yaw /= 50;
}

void updateMouseMode() {
    if (!BleMouseDevice.isConnected()) return;
    
    updateLockKeyLogic();
    updateSensKeyLogic();
    updateWheelLogic(); 
    updateNormalButtons(); 

    if (!is_airmouse_locked) {
        unsigned long now = millis();
        if (now - lastBleTime >= 12) { 
            lastBleTime = now;
            
            mpu6050_raw_data_t my_mpu_data;
            mpu6050_read_raw(&my_mpu_data); 
            int16_t *data_ptr = (int16_t *)&my_mpu_data;
            
            int16_t cal_pitch = data_ptr[5] - gyro_offset_pitch;
            int16_t cal_yaw = data_ptr[6] - gyro_offset_yaw;
            
            int16_t f_pitch = (abs(cal_pitch) > DEADZONE) ? cal_pitch : 0;
            int16_t f_yaw = (abs(cal_yaw) > DEADZONE) ? cal_yaw : 0;
            
            float target_dx = (float)f_yaw / DAMPING;
            float target_dy = (float)f_pitch / DAMPING;
            
            smooth_dx = smooth_dx + FILTER_ALPHA * (target_dx - smooth_dx);
            smooth_dy = smooth_dy + FILTER_ALPHA * (target_dy - smooth_dy);
            
            float multiplier = (float)global_sens_percent / 50.0f;
            int16_t move_x = (int16_t)(smooth_dx * multiplier);
            int16_t move_y = (int16_t)(smooth_dy * multiplier);
            
            // ====================================================
            // 🎯 【最终整型输出级死区优化】
            // ====================================================
            // 过滤低通滤波器逼近 0 时负数截断产生的恒定 ±1 像素蠕动噪声
            if (abs(move_x) <= 1) move_x = 0;
            if (abs(move_y) <= 1) move_y = 0;
            // ====================================================
            
            if (move_x != 0 || move_y != 0) {
                BleMouseDevice.move(move_x, move_y, 0);
            }
            oled_mouse_x = move_x; oled_mouse_y = move_y;
        }
    } else {
        oled_mouse_x = 0; oled_mouse_y = 0;
    }
}