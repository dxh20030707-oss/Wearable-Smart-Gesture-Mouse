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
extern Adafruit_SSD1306 display;  // OLED 显示屏实例
extern int16_t oled_mouse_x; // OLED 显示屏上的鼠标指针位置
extern int16_t oled_mouse_y; // OLED 显示屏上的鼠标指针位置
extern int16_t oled_touch_x; // OLED 显示屏上第一个触控点的 X 坐标
extern int16_t oled_touch_y; // OLED 显示屏上第一个触控点的 Y 坐标
extern bool is_airmouse_locked;  // 鼠标是否锁定

int16_t extern_mouse_x_val_if_needed();  // 获取鼠标 X 轴的值
int16_t extern_mouse_y_val_if_needed();  // 获取鼠标 Y 轴的值

// ==================== 1. 全局实体变量定义 ====================
BleMouse BleMouseDevice("ESP32-C3 SmartMouse", "Maker", 90); // 创建一个 BleMouse 实例

uint8_t global_sens_percent = 50; // 全局灵敏度百分比变量 
uint8_t global_wheel_sens_percent = 50; // 全局滚轮灵敏度百分比变量
unsigned long lastOledRefreshTime = 0; // 上次 OLED 刷新时间

int16_t global_render_dx = 0; // 全局渲染偏移量
int16_t global_render_dy = 0; // 全局渲染偏移量

SystemConfig g_cfg; // 全局系统配置结构体实例
SystemMode g_current_mode = MODE_FLY_MOUSE; // 全局当前模式

Preferences prefs; // NVS Flash 持久化实例
bool isAppConnected = false; // 是否有蓝牙设备连接
static bool lastBleState = false; // 上一次的蓝牙连接状态
unsigned long lastBleSendTime = 0;  // 上一次蓝牙发送数据的时间
unsigned long connectionStartTime = 0;  // 连接开始时间

// ----------------------------------------------------
// 2. 参数映射与 NVS Flash 持久化 (含模式记忆)
// ----------------------------------------------------
void updateConfigGains() {  // 更新全局增益参数
    g_cfg.air_gain = 0.2f + (g_cfg.air_dpi_level * 0.18f);   // 映射后的飞鼠算法增益    
    g_cfg.touch_gain = 0.5f + (g_cfg.touch_dpi_level * 0.25f);   // 映射后的触控算法增益
    
    // 同步更新全局百分比变量，保证原有 display_mode 正常渲染
    global_sens_percent = g_cfg.air_dpi_level * 10;  // 映射后的飞鼠灵敏度百分比
    global_wheel_sens_percent = g_cfg.touch_dpi_level * 10;  // 映射后的触控滚轮灵敏度百分比
}

void saveConfigToNVS() {  // 将当前配置保存到 NVS Flash 中
    prefs.begin("mouse_cfg", false); // 打开 NVS Flash 分区，非只读模式
    prefs.putUChar("air_dpi", g_cfg.air_dpi_level); // 保存飞鼠灵敏度档位
    prefs.putUChar("touch_dpi", g_cfg.touch_dpi_level); // 保存触控灵敏度档位
    prefs.putUChar("deadzone", g_cfg.deadzone_px); // 保存死区大小
    prefs.putUChar("sys_mode", (uint8_t)g_current_mode); // 保存当前系统模式
    prefs.end(); // 关闭 NVS Flash 分区
    updateConfigGains(); // 更新增益参数
    Serial.println("💾 [NVS] 参数与当前模式写入 Flash 成功！"); // 打印调试信息
}

void loadConfigFromNVS() {  // 从 NVS Flash 中加载配置
    prefs.begin("mouse_cfg", true);  // 打开 NVS Flash 分区，读取模式
    g_cfg.air_dpi_level = prefs.getUChar("air_dpi", 5);    // 默认飞鼠灵敏度档位为 5  
    g_cfg.touch_dpi_level = prefs.getUChar("touch_dpi", 5);    // 默认触控灵敏度档位为 5
    g_cfg.deadzone_px = prefs.getUChar("deadzone", 2);           // 默认死区大小为 2 px   
    
    uint8_t saved_mode = prefs.getUChar("sys_mode", 0);  // 默认系统模式为 MODE_FLY_MOUSE (0)
    if (saved_mode > 2) saved_mode = 0;  // 如果读取到的模式值不在有效范围内，则重置为 MODE_FLY_MOUSE
    g_current_mode = (SystemMode)saved_mode;  // 设置当前系统模式   
    
    prefs.end(); // 关闭 NVS Flash 分区
    updateConfigGains(); // 更新增益参数
}

// ----------------------------------------------------
// 3. 模式 3 专属 128-bit 调参蓝牙回调 (保持完整)
// ----------------------------------------------------
class AppConfigCallbacks : public NimBLECharacteristicCallbacks {  // 模式 3 蓝牙回调类
    void onWrite(NimBLECharacteristic* pCharacteristic) override {  // 当蓝牙客户端写入数据时触发
        std::string value = pCharacteristic->getValue();  // 获取写入的值 
        if (value.length() >= 5) {  // 确保写入的值长度至少为 5
            uint8_t header    = (uint8_t)value[0];   // 数据包头部，固定为 0xA5
            uint8_t cmd       = (uint8_t)value[1];   // 命令字，固定为 0x01
            uint8_t air_dpi   = (uint8_t)value[2];   // 飞鼠灵敏度档位 (1 ~ 10)
            uint8_t touch_dpi = (uint8_t)value[3];   // 触控/滚轮灵敏度档位 (1 ~ 10)
            uint8_t deadzone  = (uint8_t)value[4];   // 死区大小 (0 ~ 10)

            if (header == 0xA5) { // 验证数据包头部
                g_cfg.air_dpi_level   = constrain(air_dpi, 1, 10); // 限制飞鼠灵敏度档位在 1 ~ 10
                g_cfg.touch_dpi_level = constrain(touch_dpi, 1, 10); // 限制触控/滚轮灵敏度档位在 1 ~ 10
                g_cfg.deadzone_px     = constrain(deadzone, 0, 10); // 限制死区大小在 0 ~ 10 px

                saveConfigToNVS(); // 保存配置到 NVS Flash

                Serial.printf("📩 [模式 3 调参成功] Air:%d档 | Touch:%d档 | Deadzone:%dpx\r\n",  // 打印调试信息
                              g_cfg.air_dpi_level, g_cfg.touch_dpi_level, g_cfg.deadzone_px); // 打印调试信息
            }
        }
    }
};

class ConfigServerCallbacks : public NimBLEServerCallbacks {  // 模式 3 蓝牙服务器回调类
    void onConnect(NimBLEServer* pServer) override { isAppConnected = true; }  // 当有蓝牙客户端连接时触发，设置连接状态为 true
    void onDisconnect(NimBLEServer* pServer) override {   // 当蓝牙客户端断开连接时触发，设置连接状态为 false，并重新开始广播
        isAppConnected = false;   // 设置连接状态为 false
        NimBLEDevice::startAdvertising();  // 重新开始广播，等待新的连接
    }
};

// ----------------------------------------------------
// 4. GPIO 10 触摸按键状态机（适配 Active-HIGH：按下为 HIGH，松开为 LOW）
// ----------------------------------------------------
void handleModeSwitch() {  // 处理模式切换按键的状态机
    static unsigned long pressStartTime = 0; // 记录按键按下的时间
    static bool lastBtnState = LOW; // 触摸模块常态为 LOW
    bool currentBtnState = digitalRead(PIN_MODE_SW); // 读取当前按键状态

    // 触摸按下瞬间 (LOW -> HIGH)
    if (lastBtnState == LOW && currentBtnState == HIGH) { // 记录按键按下的时间
        pressStartTime = millis(); // 记录按键按下的时间
    } 
    // 触摸松开瞬间 (HIGH -> LOW)
    else if (lastBtnState == HIGH && currentBtnState == LOW) { // 计算按键按下的持续时间
        unsigned long pressDuration = millis() - pressStartTime; // 计算按键按下的持续时间

        if (pressDuration >= 1500) {  // 长按 1.5 秒以上：进入/退出 [模式 3]
            // 长按 1.5 秒以上：进入/退出 [模式 3]
            if (g_current_mode != MODE_APP_CONFIG) { // 如果当前不是模式 3，则切换到模式 3
                g_current_mode = MODE_APP_CONFIG; // 切换到模式 3
            } else {
                g_current_mode = MODE_FLY_MOUSE; // 如果当前是模式 3，则切换回模式 1
            }
            Serial.printf("🔄 [MODE] 长按触发，切入模式: %d\r\n", g_current_mode);   // 打印调试信息
            saveConfigToNVS();  // 保存配置到 NVS Flash
            delay(100); // 延迟 100ms
            ESP.restart();      // 重启设备
        } 
        else if (pressDuration >= 50 && g_current_mode != MODE_APP_CONFIG) {   
            // 短按：在 [模式 1：飞鼠] 和 [模式 2：触控] 之间切换
            if (g_current_mode == MODE_FLY_MOUSE) {  // 如果当前是模式 1，则切换到模式 2
                g_current_mode = MODE_TOUCH_PAD;  // 如果当前是模式 2，则切换到模式 1
            } else {
                g_current_mode = MODE_FLY_MOUSE;// 如果当前是模式 2，则切换到模式 1
            }
            Serial.printf("🔄 [MODE] 短按触发，切入模式: %d\r\n", g_current_mode);
            saveConfigToNVS(); // 保存配置到 NVS Flash
            delay(100); // 延迟 100ms
            ESP.restart(); // 重启设备
        }
    }
    lastBtnState = currentBtnState;  // 更新上一次按键状态为当前状态
}

// ----------------------------------------------------
// 5. setup 阶段初始化
// ----------------------------------------------------
void setup() {  // 初始化阶段
    Serial.begin(115200);  // 初始化串口调试输出
    
    // 触摸按键输出高电平，采用标准 INPUT 模式
    pinMode(PIN_MODE_SW, INPUT);  // 设置触摸按键为输入模式

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);  //   初始化 I2C 总线，指定 SDA 和 SCL 引脚
    Wire.setClock(400000);  // 设置 I2C 时钟频率为 400kHz
    Wire.setTimeOut(10);   // 设置 I2C 超时时间为 10ms
    delay(100); // 延迟 100ms，等待 I2C 总线稳定

    initDisplayMode(); // 初始化 OLED 显示屏

    // 从 Flash 读取保存的模式与参数 
    loadConfigFromNVS(); // 从 Flash 读取保存的模式与参数

    if (g_current_mode == MODE_APP_CONFIG) {  // 如果当前模式是模式 3，则初始化蓝牙广播
        // ==================== [模式 3：手机 128-bit 专属调参模式] ====================
        Serial.println("⚙️ [MODE 3] 启动手机 128-bit 专属调参模式"); // 打印调试信息
        
        NimBLEDevice::init("SmartMouse-Config"); // 初始化蓝牙设备
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // 设置蓝牙发射功率为 +9dBm

        NimBLEServer* pServer = NimBLEDevice::createServer(); // 创建蓝牙服务器
        pServer->setCallbacks(new ConfigServerCallbacks());  // 设置蓝牙服务器回调函数

        NimBLEService* pService = pServer->createService(CONFIG_SERVICE_UUID); // 创建蓝牙服务
        NimBLECharacteristic* pChar = pService->createCharacteristic( // 创建蓝牙特征值
            CONFIG_CHAR_UUID, // 特征值 UUID
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR // 特征值属性
        );
        pChar->setCallbacks(new AppConfigCallbacks()); // 设置蓝牙特征值回调函数
        pService->start(); // 启动蓝牙服务

        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising(); // 获取蓝牙广播对象
        pAdvertising->addServiceUUID(CONFIG_SERVICE_UUID); // 添加蓝牙服务 UUID 到广播数据中
        pAdvertising->start(); // 开始蓝牙广播

    } else {
        // ==================== [模式 1 / 模式 2：标准 BLE HID 鼠标模式] ====================
        BleMouseDevice.begin(); // 启动蓝牙鼠标设备
        NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // 设置蓝牙发射功率为 +9dBm
        Serial.println("[蓝牙系统] 广播已就绪，等待无锁秒连..."); // 打印调试信息

        if (g_current_mode == MODE_FLY_MOUSE) {  // 如果当前模式是模式 1，则初始化飞鼠模式
            initMouseMode();    // 初始化飞鼠模式
        } else if (g_current_mode == MODE_TOUCH_PAD) {  // 如果当前模式是模式 2，则初始化触控模式
            initTouchMode();  // 初始化触控模式
        }
    }
}

// ----------------------------------------------------
// 6. loop 主循环
// ----------------------------------------------------
void loop() {  // 主循环
    unsigned long now = millis(); // 获取当前时间戳
    handleModeSwitch(); // 扫描按键

    if (g_current_mode == MODE_APP_CONFIG) { // 如果当前模式是模式 3，则保持蓝牙广播
        // 模式 3 下维持蓝牙广播
        NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising(); // 获取蓝牙广播对象
        if (pAdv && !pAdv->isAdvertising()) pAdv->start(); // 如果蓝牙广播未启动，则重新启动广播

    } else {  // 如果当前模式是模式 1 或模式 2，则处理鼠标移动与触控逻辑
        // 模式 1 与 模式 2 处理
        bool is_connected = BleMouseDevice.isConnected();  // 获取蓝牙连接状态

        if (is_connected && !lastBleState) {  // 如果当前连接状态为已连接，并且上一次状态为未连接，则记录连接开始时间
            connectionStartTime = now;  // 记录连接开始时间 
            Serial.println("[蓝牙] 成功建立全新物理连接！");  // 打印调试信息
        }
        lastBleState = is_connected;  // 更新上一次的蓝牙连接状态为当前状态

        if (now - lastBleSendTime >= 15) {  // 每 15ms 处理一次鼠标移动与触控逻辑
            lastBleSendTime = now;  // 更新上一次蓝牙发送时间

            if (is_connected) {  // 如果当前连接状态为已连接，则处理鼠标移动与触控逻辑
                if (now - connectionStartTime > 2000) {   // 如果距离连接开始时间超过 2 秒，则处理鼠标移动与触控逻辑
                    if (g_current_mode == MODE_FLY_MOUSE) {  // 如果当前模式是模式 1，则处理飞鼠逻辑
                        updateMouseMode();    // 处理飞鼠逻辑
                        global_render_dx = extern_mouse_x_val_if_needed();  // 从外部获取鼠标 X 坐标
                        global_render_dy = extern_mouse_y_val_if_needed();  // 从外部获取鼠标 Y 坐标
                    } 
                    else if (g_current_mode == MODE_TOUCH_PAD) { // 如果当前模式是模式 2，则处理触控逻辑
                        updateTouchMode();   // 处理触控逻辑
                    }
                }
            }
        }
    }

    // OLED 界面高频渲染 (100ms)
    if (now - lastOledRefreshTime >= 100) { // 每 100ms 刷新一次 OLED 显示屏
        lastOledRefreshTime = now;  // 更新上一次 OLED 刷新时间

        if (g_current_mode == MODE_FLY_MOUSE) {  // 如果当前模式是模式 1，则更新飞鼠模式的 OLED 显示屏
            updateDisplayMode1(oled_mouse_x, oled_mouse_y, (float)global_sens_percent, BleMouseDevice.isConnected());  // 更新飞鼠模式的 OLED 显示屏
        } 
        else if (g_current_mode == MODE_TOUCH_PAD) {  // 如果当前模式是模式 2，则更新触控模式的 OLED 显示屏
            updateDisplayMode2(oled_touch_x, oled_touch_y, (float)global_sens_percent, BleMouseDevice.isConnected()); // 更新触控模式的 OLED 显示屏
        } 
        else if (g_current_mode == MODE_APP_CONFIG) { // 如果当前模式是模式 3，则更新配置模式的 OLED 显示屏
            display.clearDisplay(); // 清空 OLED 显示屏
            display.setTextSize(1); // 设置文本大小为 1
            display.setTextColor(SSD1306_WHITE);  // 设置文本颜色为白色

            display.setCursor(0, 0);  // 设置光标位置为 (0, 0)
            display.println("== MODE 3: CONFIG ==");  // 设置 OLED 显示屏的标题
            display.drawLine(0, 10, 128, 10, SSD1306_WHITE);  // 绘制分割线
            
            display.setCursor(0, 16);  // 设置光标位置为 (0, 16)
            display.printf("Status   : %s", isAppConnected ? "CONNECTED" : "WAITING..."); // 设置 OLED 显示屏的连接状态
            display.setCursor(0, 28);  // 设置光标位置为 (0, 28)
            display.printf("Air DPI  : %d (%d%%)", g_cfg.air_dpi_level, g_cfg.air_dpi_level * 10);  // 设置 OLED 显示屏的飞鼠灵敏度档位
            display.setCursor(0, 40); // 设置光标位置为 (0, 40)
            display.printf("TouchDPI : %d (%d%%)", g_cfg.touch_dpi_level, g_cfg.touch_dpi_level * 10); // 设置 OLED 显示屏的触控灵敏度档位
            display.setCursor(0, 52);  // 设置光标位置为 (0, 52)
            display.printf("Deadzone : %d px", g_cfg.deadzone_px);  // 设置 OLED 显示屏的死区大小

            display.display(); // 刷新 OLED 显示屏
        }
    }

    delay(6);   // 延迟 6ms，降低 CPU 占用率，避免 OLED 显示屏闪烁
}

int16_t extern_mouse_x_val_if_needed() { return oled_mouse_x; }  // 返回鼠标 X 坐标
int16_t extern_mouse_y_val_if_needed() { return oled_mouse_y; }  // 返回鼠标 Y 坐标