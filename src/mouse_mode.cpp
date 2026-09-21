#include "mouse_mode.h"
#include "config.h"
#include "mpu6050_driver.h" 
#include <BleMouse.h>
#include <Arduino.h>
#include <Wire.h>

extern BleMouse BleMouseDevice;
extern uint8_t global_sens_percent; 
extern uint8_t global_wheel_sens_percent; 
extern SystemConfig g_cfg;

int16_t oled_mouse_x = 0;
int16_t oled_mouse_y = 0;
bool is_airmouse_locked = false; 

// 供 display_mode.cpp 渲染使用的滚轮计数变量
volatile int32_t mouseWheelCount = 0;

int32_t gyro_offset_pitch = 0; 
int32_t gyro_offset_yaw = 0;   

const int16_t DAMPING = 120;    
static float smooth_dx = 0;
static float smooth_dy = 0;
const float FILTER_ALPHA = 0.4f; 
static unsigned long lastBleTime = 0;

// ====================================================
// 🎯 滚轮平滑动力学累加器与定时器
// ====================================================
static float scroll_accumulator_up = 0.0f;
static float scroll_accumulator_down = 0.0f;
static unsigned long lastWheelTickTime = 0;

// 触摸按键逻辑电平宏（点动高电平）
#define TOUCH_PRESSED   HIGH
#define TOUCH_RELEASED  LOW

// ====================================================
// 🎯 按键与滚轮核心分流逻辑
// ====================================================
static void updateNormalButtons() {
    static bool lastLeftState = TOUCH_RELEASED;
    static bool lastRightState = TOUCH_RELEASED;
    static unsigned long lastLeftDebounce = 0;
    static unsigned long lastRightDebounce = 0;

    bool currLeft = digitalRead(PIN_M1_LEFT);   
    bool currRight = digitalRead(PIN_M1_RIGHT); 
    unsigned long now = millis();

    // ----------------------------------------------------
    // 情况 A：正常飞鼠光标模式 (!is_airmouse_locked)
    // ----------------------------------------------------
    if (!is_airmouse_locked) {
        scroll_accumulator_up = 0.0f;
        scroll_accumulator_down = 0.0f;

        // 左键处理
        if (currLeft != lastLeftState) {
            if (now - lastLeftDebounce > 20) { 
                lastLeftDebounce = now;
                if (currLeft == TOUCH_PRESSED) {
                    BleMouseDevice.press(MOUSE_LEFT);
                } else {
                    BleMouseDevice.release(MOUSE_LEFT);
                }
                lastLeftState = currLeft;
            }
        }

        // 右键处理
        if (currRight != lastRightState) {
            if (now - lastRightDebounce > 20) {
                lastRightDebounce = now;
                if (currRight == TOUCH_PRESSED) {
                    BleMouseDevice.press(MOUSE_RIGHT);
                } else {
                    BleMouseDevice.release(MOUSE_RIGHT);
                }
                lastRightState = currRight;
            }
        }
    } 
    // ----------------------------------------------------
    // 情况 B：滚轮锁定模式 (is_airmouse_locked) -> 触摸键控上下顺滑滚动
    // ----------------------------------------------------
    else {
        // --- 1. 左键（向上滚动）触摸检测 ---
        if (currLeft != lastLeftState) {
            if (now - lastLeftDebounce > 20) {
                lastLeftDebounce = now;
                lastLeftState = currLeft;
                if (currLeft == TOUCH_PRESSED) {
                    // 触摸瞬间立刻响应 1 格首包（零延迟触感）
                    BleMouseDevice.move(0, 0, 1);
                    mouseWheelCount += 1;
                    scroll_accumulator_up = 0.0f;
                } else {
                    // 松开手指立刻刹车清零
                    scroll_accumulator_up = 0.0f;
                }
            }
        }

        // --- 2. 右键（向下滚动）触摸检测 ---
        if (currRight != lastRightState) {
            if (now - lastRightDebounce > 20) {
                lastRightDebounce = now;
                lastRightState = currRight;
                if (currRight == TOUCH_PRESSED) {
                    // 触摸瞬间立刻响应 1 格首包（零延迟触感）
                    BleMouseDevice.move(0, 0, -1);
                    mouseWheelCount -= 1;
                    scroll_accumulator_down = 0.0f;
                } else {
                    // 松开手指立刻刹车清零
                    scroll_accumulator_down = 0.0f;
                }
            }
        }

        // --- 3. 连续触摸持续按住：高帧率平滑动力学连发 (20ms 刷新周期) ---
        if (now - lastWheelTickTime >= 20) {
            lastWheelTickTime = now;

            // 根据滚轮灵敏度 (10% ~ 100%) 计算每 20ms 的速度增量
            // 10% 灵敏度 -> 速度 0.12 (约 160ms 滚 1 格，平缓细腻)
            // 50% 灵敏度 -> 速度 0.35 (约 57ms 滚 1 格，标准顺滑)
            // 100% 灵敏度 -> 速度 0.85 (约 23ms 滚 1 格，高速滑动)
            float speed = 0.05f + ((float)global_wheel_sens_percent / 100.0f) * 0.80f;

            // 左键持续触摸：向上平滑累加
            if (currLeft == TOUCH_PRESSED && lastLeftState == TOUCH_PRESSED) {
                scroll_accumulator_up += speed;
                if (scroll_accumulator_up >= 1.0f) {
                    int8_t step = (int8_t)scroll_accumulator_up;
                    scroll_accumulator_up -= step;
                    BleMouseDevice.move(0, 0, step);
                    mouseWheelCount += step;
                }
            }

            // 右键持续触摸：向下平滑累加
            if (currRight == TOUCH_PRESSED && lastRightState == TOUCH_PRESSED) {
                scroll_accumulator_down += speed;
                if (scroll_accumulator_down >= 1.0f) {
                    int8_t step = (int8_t)scroll_accumulator_down;
                    scroll_accumulator_down -= step;
                    BleMouseDevice.move(0, 0, -step);
                    mouseWheelCount -= step;
                }
            }
        }
    }
}

// ====================================================
// 🎯 模式 1 锁定/断控切换逻辑 (GPIO 2 触摸按键)
// ====================================================
static void updateLockKeyLogic() {
    static bool lastLockBtnState = TOUCH_RELEASED;
    static unsigned long lockBtnDebounceTime = 0;
    bool currentLockState = digitalRead(PIN_M1_PAUSE); 
    
    if (currentLockState != lastLockBtnState) {
        if (millis() - lockBtnDebounceTime > 50) { 
            lockBtnDebounceTime = millis();
            // 触摸按下瞬间 (LOW -> HIGH)
            if (currentLockState == TOUCH_PRESSED) { 
                is_airmouse_locked = !is_airmouse_locked;
                
                // 重置滚轮计数、累加器与光标滤波残余
                mouseWheelCount = 0;
                scroll_accumulator_up = 0.0f;
                scroll_accumulator_down = 0.0f;
                smooth_dx = 0;
                smooth_dy = 0;

                BleMouseDevice.release(MOUSE_LEFT);
                BleMouseDevice.release(MOUSE_RIGHT);

                Serial.printf("🖱️ [M1] 状态切换 -> 滚轮锁定模式: %s\r\n", is_airmouse_locked ? "开启" : "关闭");
            }
            lastLockBtnState = currentLockState;
        }
    }
}

// ====================================================
// 🎯 灵敏度调节逻辑 (GPIO 3 触摸按键)
// ====================================================
static void updateSensKeyLogic() {
    static bool lastSensBtnState = TOUCH_RELEASED;
    static unsigned long sensBtnPressTime = 0;
    static unsigned long lastAutoStepTime = 0;
    static bool isLongPressed = false;
    bool currentSensState = digitalRead(PIN_M1_SENS); 

    // 触摸按下瞬间 (LOW -> HIGH)
    if (currentSensState == TOUCH_PRESSED && lastSensBtnState == TOUCH_RELEASED) {
        sensBtnPressTime = millis();
        isLongPressed = false;
        delay(5); 
    }
    
    // 持续触摸 (按住)
    if (currentSensState == TOUCH_PRESSED) {
        if (millis() - sensBtnPressTime >= 400) { 
            isLongPressed = true;
            if (millis() - lastAutoStepTime >= 150) { 
                lastAutoStepTime = millis();
                if (!is_airmouse_locked) {
                    global_sens_percent += 10;
                    if (global_sens_percent > 100) global_sens_percent = 10;
                    g_cfg.air_dpi_level = global_sens_percent / 10;
                } else {
                    global_wheel_sens_percent += 10;
                    if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10;
                    g_cfg.touch_dpi_level = global_wheel_sens_percent / 10;
                }
                saveConfigToNVS();
            }
        }
    }
    
    // 触摸松开瞬间 (HIGH -> LOW)
    if (currentSensState == TOUCH_RELEASED && lastSensBtnState == TOUCH_PRESSED) {
        if (!isLongPressed && (millis() - sensBtnPressTime > 25)) {
            if (!is_airmouse_locked) {
                global_sens_percent += 10;
                if (global_sens_percent > 100) global_sens_percent = 10;
                g_cfg.air_dpi_level = global_sens_percent / 10;
            } else {
                global_wheel_sens_percent += 10;
                if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10;
                g_cfg.touch_dpi_level = global_wheel_sens_percent / 10;
            }
            saveConfigToNVS();
        }
    }
    lastSensBtnState = currentSensState;
}

// ====================================================
// 🎯 初始化与主更新循环
// ====================================================
void initMouseMode() {
    is_airmouse_locked = false;
    mouseWheelCount = 0;
    scroll_accumulator_up = 0.0f;
    scroll_accumulator_down = 0.0f;
    smooth_dx = 0;
    smooth_dy = 0;
    
    // 触摸模块输出高电平，采用标准 INPUT 模式
    pinMode(PIN_M1_LEFT, INPUT);   
    pinMode(PIN_M1_RIGHT, INPUT);  
    pinMode(PIN_M1_PAUSE, INPUT);  
    pinMode(PIN_M1_SENS, INPUT);   

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
    Serial.println("[M1] 触摸按键版飞鼠模式初始化完成。");
}

void updateMouseMode() {
    if (!BleMouseDevice.isConnected()) return;
    
    updateLockKeyLogic();   
    updateSensKeyLogic();   
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
            
            int16_t dynamic_deadzone = (int16_t)(g_cfg.deadzone_px * 40); 
            int16_t f_pitch = (abs(cal_pitch) > dynamic_deadzone) ? cal_pitch : 0;
            int16_t f_yaw = (abs(cal_yaw) > dynamic_deadzone) ? cal_yaw : 0;
            
            float target_dx = (float)f_yaw / DAMPING;
            float target_dy = (float)f_pitch / DAMPING;
            
            smooth_dx = smooth_dx + FILTER_ALPHA * (target_dx - smooth_dx);
            smooth_dy = smooth_dy + FILTER_ALPHA * (target_dy - smooth_dy);
            
            int16_t move_x = (int16_t)(smooth_dx * g_cfg.air_gain);
            int16_t move_y = (int16_t)(smooth_dy * g_cfg.air_gain);
            
            if (abs(move_x) <= g_cfg.deadzone_px) move_x = 0;
            if (abs(move_y) <= g_cfg.deadzone_px) move_y = 0;
            
            if (move_x != 0 || move_y != 0) {
                BleMouseDevice.move(move_x, move_y, 0);
            }
            oled_mouse_x = move_x; oled_mouse_y = move_y;
        }
    } else {
        oled_mouse_x = 0; oled_mouse_y = 0;
    }
}