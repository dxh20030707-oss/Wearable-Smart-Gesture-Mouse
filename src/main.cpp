#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "mpu6050_driver.h"
#include "display_mode.h"
#include "mouse_mode.h"
#include <BleMouse.h>
#include <Adafruit_SSD1306.h>
#include <NimBLEDevice.h>

int16_t extern_mouse_x_val_if_needed();
int16_t extern_mouse_y_val_if_needed();
extern void initTouchMode();
extern void updateTouchMode();

extern Adafruit_SSD1306 display;

BleMouse BleMouseDevice("ESP32-C3 SmartMouse", "Maker", 90);

float sensitivityList[4] = {0.5f, 1.0f, 1.5f, 2.0f};
uint8_t currentSensIdx = 1; 

uint8_t global_sens_percent = 50;
uint8_t global_wheel_sens_percent = 50;
uint8_t current_system_mode = 1; 

unsigned long lastBleSendTime = 0;
unsigned long lastOledRefreshTime = 0;
unsigned long connectionStartTime = 0; 

extern int32_t gyro_offset_pitch;
extern int32_t gyro_offset_yaw;

int16_t global_render_dx = 0;
int16_t global_render_dy = 0;

extern bool is_airmouse_locked;
extern int16_t oled_touch_x;
extern int16_t oled_touch_y;

static bool lastBleState = false;

// 🎯 全局唯一实体定义
bool is_ctrl_pressed = false; 

void switchSystemMode(uint8_t targetMode) {
    Serial.printf("[系统] 正在准备从模式 %d 切换到模式 %d...\r\n", current_system_mode, targetMode);
    
    detachInterrupt(digitalPinToInterrupt(0));
    detachInterrupt(digitalPinToInterrupt(1));
    detachInterrupt(digitalPinToInterrupt(2));
    detachInterrupt(digitalPinToInterrupt(3));
    
    pinMode(0, INPUT);
    pinMode(1, INPUT);
    pinMode(2, INPUT);
    pinMode(3, INPUT);
    
    current_system_mode = targetMode;
    if (current_system_mode == 1) {
        initMouseMode();
    } else if (current_system_mode == 2) {
        initTouchMode();
    }
    Serial.printf("[系统] 成功切入模式 %d！\r\n", current_system_mode);
}

void checkModeSwitchKey() {
    static bool lastSwitchBtnState = HIGH;
    static unsigned long lastSwitchDebounce = 0;
    
    bool currentBtnState = digitalRead(10); 
    if (currentBtnState != lastSwitchBtnState) {
        if (millis() - lastSwitchDebounce > 60) {
            lastSwitchDebounce = millis();
            if (currentBtnState == LOW) { 
                uint8_t nextMode = (current_system_mode == 1) ? 2 : 1;
                switchSystemMode(nextMode);
            }
            lastSwitchBtnState = currentBtnState;
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(10, INPUT_PULLUP);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    Wire.setTimeOut(10); 
    delay(100);

    initDisplayMode();

    BleMouseDevice.begin();
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    
    Serial.println("[蓝牙系统] 广播已就绪，等待无锁秒连...");
    
    initMouseMode();
}

void loop() {
    unsigned long now = millis();
    checkModeSwitchKey();

    bool is_connected = BleMouseDevice.isConnected();

    if (is_connected && !lastBleState) {
        connectionStartTime = now;
        Serial.println("[蓝牙] 成功建立全新物理连接！");
    }
    lastBleState = is_connected;

    if (now - lastBleSendTime >= 15) {
        lastBleSendTime = now;

        if (is_connected) {
            if (now - connectionStartTime > 2000) {
                if (current_system_mode == 1) {
                    updateMouseMode(); 
                    global_render_dx = extern_mouse_x_val_if_needed(); 
                    global_render_dy = extern_mouse_y_val_if_needed(); 
                } 
                else if (current_system_mode == 2) {
                    updateTouchMode(); 
                }
            }
        }
    }

    if (now - lastOledRefreshTime >= 100) {
        lastOledRefreshTime = now;

        extern int16_t oled_mouse_x;
        extern int16_t oled_mouse_y;

        if (current_system_mode == 1) {
            updateDisplayMode1(oled_mouse_x, oled_mouse_y, (float)global_sens_percent, is_connected);
        } 
        else if (current_system_mode == 2) {
            updateDisplayMode2(oled_touch_x, oled_touch_y, (float)global_sens_percent, is_connected);
        }
    }

    delay(6); 
}

int16_t extern_mouse_x_val_if_needed() { extern int16_t oled_mouse_x; return oled_mouse_x; }
int16_t extern_mouse_y_val_if_needed() { extern int16_t oled_mouse_y; return oled_mouse_y; }