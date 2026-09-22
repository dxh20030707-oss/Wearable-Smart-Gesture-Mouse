#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <BleMouse.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

#include "config.h"
#include "mpu6050_driver.h"
#include "display_mode.h"
#include "mouse_mode.h"
#include "touch_mode.h"

// 外部驱动与变量声明
extern Adafruit_SSD1306 display;
extern int16_t oled_mouse_x;
extern int16_t oled_mouse_y;
extern int16_t oled_touch_x;
extern int16_t oled_touch_y;
extern bool is_airmouse_locked;

int16_t extern_mouse_x_val_if_needed();
int16_t extern_mouse_y_val_if_needed();

// ==================== 1. 全局实体变量定义 ====================
BleMouse BleMouseDevice("ESP32-C3 SmartMouse", "Maker", 90);

uint8_t global_sens_percent = 50;
uint8_t global_wheel_sens_percent = 50;
unsigned long lastOledRefreshTime = 0;

int16_t global_render_dx = 0;
int16_t global_render_dy = 0;

SystemConfig g_cfg;
SystemMode g_current_mode = MODE_FLY_MOUSE;

Preferences prefs;
bool isAppConnected = false;
static bool lastBleState = false;
unsigned long lastBleSendTime = 0;
unsigned long connectionStartTime = 0;

// ----------------------------------------------------
// 2. 参数映射与 NVS Flash 持久化 (含模式记忆)
// ----------------------------------------------------
void updateConfigGains() {
    g_cfg.air_gain = 0.2f + (g_cfg.air_dpi_level * 0.18f);      
    g_cfg.touch_gain = 0.5f + (g_cfg.touch_dpi_level * 0.25f);  
    
    // 同步更新全局百分比变量，保证原有 display_mode 正常渲染
    global_sens_percent = g_cfg.air_dpi_level * 10;
    global_wheel_sens_percent = g_cfg.touch_dpi_level * 10;
}

void saveConfigToNVS() {
    prefs.begin("mouse_cfg", false);
    prefs.putUChar("air_dpi", g_cfg.air_dpi_level);
    prefs.putUChar("touch_dpi", g_cfg.touch_dpi_level);
    prefs.putUChar("deadzone", g_cfg.deadzone_px);
    prefs.putUChar("sys_mode", (uint8_t)g_current_mode); // 模式落盘
    prefs.end();
    updateConfigGains();
    Serial.println("💾 [NVS] 参数与当前模式写入 Flash 成功！");
}

void loadConfigFromNVS() {
    prefs.begin("mouse_cfg", true);
    g_cfg.air_dpi_level = prefs.getUChar("air_dpi", 5);     
    g_cfg.touch_dpi_level = prefs.getUChar("touch_dpi", 5);   
    g_cfg.deadzone_px = prefs.getUChar("deadzone", 2);        
    
    uint8_t saved_mode = prefs.getUChar("sys_mode", 0);
    if (saved_mode > 2) saved_mode = 0;
    g_current_mode = (SystemMode)saved_mode;
    
    prefs.end();
    updateConfigGains();
}

// ----------------------------------------------------
// 3. 模式 3 专属 128-bit 调参蓝牙回调 (保持完整)
// ----------------------------------------------------
class AppConfigCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (value.length() >= 5) {
            uint8_t header    = (uint8_t)value[0]; 
            uint8_t cmd       = (uint8_t)value[1]; 
            uint8_t air_dpi   = (uint8_t)value[2]; 
            uint8_t touch_dpi = (uint8_t)value[3]; 
            uint8_t deadzone  = (uint8_t)value[4]; 

            if (header == 0xA5) {
                g_cfg.air_dpi_level   = constrain(air_dpi, 1, 10);
                g_cfg.touch_dpi_level = constrain(touch_dpi, 1, 10);
                g_cfg.deadzone_px     = constrain(deadzone, 0, 10);

                saveConfigToNVS();

                Serial.printf("📩 [模式 3 调参成功] Air:%d档 | Touch:%d档 | Deadzone:%dpx\r\n", 
                              g_cfg.air_dpi_level, g_cfg.touch_dpi_level, g_cfg.deadzone_px);
            }
        }
    }
};

class ConfigServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) override { isAppConnected = true; }
    void onDisconnect(NimBLEServer* pServer) override { 
        isAppConnected = false; 
        NimBLEDevice::startAdvertising();
    }
};

// ----------------------------------------------------
// 4. GPIO 10 触摸按键状态机（适配 Active-HIGH：按下为 HIGH，松开为 LOW）
// ----------------------------------------------------
void handleModeSwitch() {
    static unsigned long pressStartTime = 0;
    static bool lastBtnState = LOW; // 触摸模块常态为 LOW
    bool currentBtnState = digitalRead(PIN_MODE_SW);

    // 触摸按下瞬间 (LOW -> HIGH)
    if (lastBtnState == LOW && currentBtnState == HIGH) {
        pressStartTime = millis();
    } 
    // 触摸松开瞬间 (HIGH -> LOW)
    else if (lastBtnState == HIGH && currentBtnState == LOW) {
        unsigned long pressDuration = millis() - pressStartTime;

        if (pressDuration >= 1500) { 
            // 长按 1.5 秒以上：进入/退出 [模式 3]
            if (g_current_mode != MODE_APP_CONFIG) {
                g_current_mode = MODE_APP_CONFIG;
            } else {
                g_current_mode = MODE_FLY_MOUSE;
            }
            Serial.printf("🔄 [MODE] 长按触发，切入模式: %d\r\n", g_current_mode);
            saveConfigToNVS();
            delay(100);
            ESP.restart(); 
        } 
        else if (pressDuration >= 50 && g_current_mode != MODE_APP_CONFIG) { 
            // 短按：在 [模式 1：飞鼠] 和 [模式 2：触控] 之间切换
            if (g_current_mode == MODE_FLY_MOUSE) {
                g_current_mode = MODE_TOUCH_PAD;
            } else {
                g_current_mode = MODE_FLY_MOUSE;
            }
            Serial.printf("🔄 [MODE] 短按触发，切入模式: %d\r\n", g_current_mode);
            saveConfigToNVS();
            delay(100);
            ESP.restart();
        }
    }
    lastBtnState = currentBtnState;
}

// ----------------------------------------------------
// 5. setup 阶段初始化
// ----------------------------------------------------
void setup() {
    Serial.begin(115200);
    
    // 触摸按键输出高电平，采用标准 INPUT 模式
    pinMode(PIN_MODE_SW, INPUT);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(400000);
    Wire.setTimeOut(10); 
    delay(100);

    initDisplayMode();

    // 从 Flash 读取保存的模式与参数
    loadConfigFromNVS();

    if (g_current_mode == MODE_APP_CONFIG) {
        // ==================== [模式 3：手机 128-bit 专属调参模式] ====================
        Serial.println("⚙️ [MODE 3] 启动手机 128-bit 专属调参模式");
        
        NimBLEDevice::init("SmartMouse-Config");
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);

        NimBLEServer* pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new ConfigServerCallbacks());

        NimBLEService* pService = pServer->createService(CONFIG_SERVICE_UUID);
        NimBLECharacteristic* pChar = pService->createCharacteristic(
            CONFIG_CHAR_UUID,
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        pChar->setCallbacks(new AppConfigCallbacks());
        pService->start();

        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(CONFIG_SERVICE_UUID);
        pAdvertising->start();

    } else {
        // ==================== [模式 1 / 模式 2：标准 BLE HID 鼠标模式] ====================
        BleMouseDevice.begin();
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);
        Serial.println("[蓝牙系统] 广播已就绪，等待无锁秒连...");

        if (g_current_mode == MODE_FLY_MOUSE) {
            initMouseMode();
        } else if (g_current_mode == MODE_TOUCH_PAD) {
            initTouchMode();
        }
    }
}

// ----------------------------------------------------
// 6. loop 主循环
// ----------------------------------------------------
void loop() {
    unsigned long now = millis();
    handleModeSwitch(); // 扫描按键

    if (g_current_mode == MODE_APP_CONFIG) {
        // 模式 3 下维持蓝牙广播
        NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
        if (pAdv && !pAdv->isAdvertising()) pAdv->start();

    } else {
        // 模式 1 与 模式 2 处理
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
                    if (g_current_mode == MODE_FLY_MOUSE) {
                        updateMouseMode(); 
                        global_render_dx = extern_mouse_x_val_if_needed(); 
                        global_render_dy = extern_mouse_y_val_if_needed(); 
                    } 
                    else if (g_current_mode == MODE_TOUCH_PAD) {
                        updateTouchMode(); 
                    }
                }
            }
        }
    }

    // OLED 界面高频渲染 (100ms)
    if (now - lastOledRefreshTime >= 100) {
        lastOledRefreshTime = now;

        if (g_current_mode == MODE_FLY_MOUSE) {
            updateDisplayMode1(oled_mouse_x, oled_mouse_y, (float)global_sens_percent, BleMouseDevice.isConnected());
        } 
        else if (g_current_mode == MODE_TOUCH_PAD) {
            updateDisplayMode2(oled_touch_x, oled_touch_y, (float)global_sens_percent, BleMouseDevice.isConnected());
        } 
        else if (g_current_mode == MODE_APP_CONFIG) {
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);

            display.setCursor(0, 0);
            display.println("== MODE 3: CONFIG ==");
            display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
            
            display.setCursor(0, 16);
            display.printf("Status   : %s", isAppConnected ? "CONNECTED" : "WAITING...");
            display.setCursor(0, 28);
            display.printf("Air DPI  : %d (%d%%)", g_cfg.air_dpi_level, g_cfg.air_dpi_level * 10);
            display.setCursor(0, 40);
            display.printf("TouchDPI : %d (%d%%)", g_cfg.touch_dpi_level, g_cfg.touch_dpi_level * 10);
            display.setCursor(0, 52);
            display.printf("Deadzone : %d px", g_cfg.deadzone_px);

            display.display();
        }
    }

    delay(6); 
}

int16_t extern_mouse_x_val_if_needed() { return oled_mouse_x; }
int16_t extern_mouse_y_val_if_needed() { return oled_mouse_y; }