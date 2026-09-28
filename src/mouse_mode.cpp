#include "mouse_mode.h" // 鼠标模式头文件
#include "config.h" // 配置头文件
#include "mpu6050_driver.h"  // 引入 MPU6050 驱动头文件
#include <BleMouse.h>  // 引入 ESP32 BLE Mouse 库头文件
#include <Arduino.h>  // 引入 Arduino 库头文件
#include <Wire.h>  // 引入 Wire 库头文件以支持 I2C 通信

extern BleMouse BleMouseDevice; // 外部声明 BleMouseDevice 对象，供全局使用
extern uint8_t global_sens_percent;   // 飞鼠灵敏度百分比 (1 ~ 100)
extern uint8_t global_wheel_sens_percent;   // 滚轮灵敏度百分比 (1 ~ 100)
extern SystemConfig g_cfg;  // 外部声明全局配置结构体 g_cfg，供全局使用

int16_t oled_mouse_x = 0;  // 供 display_mode.cpp 渲染使用的飞鼠光标 X 坐标 
int16_t oled_mouse_y = 0;  // 供 display_mode.cpp 渲染使用的飞鼠光标 Y 坐标
bool is_airmouse_locked = false;  // 供 display_mode.cpp 渲染使用的飞鼠锁定状态标志

// 供 display_mode.cpp 渲染使用的滚轮计数变量
volatile int32_t mouseWheelCount = 0;

int32_t gyro_offset_pitch = 0;  // 陀螺仪偏移量 (俯仰角)
int32_t gyro_offset_yaw = 0;    // 陀螺仪偏移量 (偏航角)

const int16_t DAMPING = 120;     // 飞鼠阻尼系数 (数值越大，飞鼠移动越平滑，灵敏度越低)
static float smooth_dx = 0;      // 飞鼠平滑滤波累积器 (X 轴)
static float smooth_dy = 0;         // 飞鼠平滑滤波累积器 (Y 轴)
const float FILTER_ALPHA = 0.4f;   // 飞鼠平滑滤波系数 (0.0 ~ 1.0, 数值越大，飞鼠移动越平滑，灵敏度越低)
static unsigned long lastBleTime = 0; // 上次 BLE 鼠标数据发送时间戳 (毫秒)

// ====================================================
// 🎯 滚轮平滑动力学累加器与定时器
// ====================================================
static float scroll_accumulator_up = 0.0f; // 向上滚动累加器 
static float scroll_accumulator_down = 0.0f; // 向下滚动累加器
static unsigned long lastWheelTickTime = 0; // 上次滚轮平滑动力学更新的时间戳 (毫秒)

// 供 OLED 显示使用的实时滚动方向（+1=向上滚, -1=向下滚, 0=静止）
int8_t g_scroll_dir = 0;

// 触摸按键逻辑电平宏（点动高电平）
#define TOUCH_PRESSED   HIGH// 触摸按键按下状态 (HIGH)
#define TOUCH_RELEASED  LOW // 触摸按键释放状态 (LOW)

// ====================================================
// 🎯 按键与滚轮核心分流逻辑
// ====================================================
static void updateNormalButtons() { // 非滚轮锁定模式下的按键处理
    static bool lastLeftState = TOUCH_RELEASED; // 上次左键状态
    static bool lastRightState = TOUCH_RELEASED; // 上次右键状态
    static unsigned long lastLeftDebounce = 0; // 上次左键去抖动时间戳 (毫秒) 
    static unsigned long lastRightDebounce = 0; // 上次右键去抖动时间戳 (毫秒)

    bool currLeft = digitalRead(PIN_M1_LEFT);   // 当前左键状态 
    bool currRight = digitalRead(PIN_M1_RIGHT);  // 当前右键状态
    unsigned long now = millis(); // 当前时间戳 (毫秒)

    // ----------------------------------------------------
    // 情况 A：正常飞鼠光标模式 (!is_airmouse_locked)
    // ----------------------------------------------------
    if (!is_airmouse_locked) { // 非滚轮锁定模式下，按键直接映射为鼠标点击
        scroll_accumulator_up = 0.0f; // 重置向上滚动累加器
        scroll_accumulator_down = 0.0f; // 重置向下滚动累加器

        // 左键处理
        if (currLeft != lastLeftState) { // 检测左键状态变化
            if (now - lastLeftDebounce > 20) {  // 去抖动处理，20ms 内忽略状态变化
                lastLeftDebounce = now;  // 更新时间戳,记录当前时间
                if (currLeft == TOUCH_PRESSED) { // 左键按下，发送鼠标左键按下事件
                    BleMouseDevice.press(MOUSE_LEFT); // 发送左键按下事件
                } else {
                    BleMouseDevice.release(MOUSE_LEFT);// 左键松开，发送鼠标左键释放事件
                }
                lastLeftState = currLeft; // 更新上次左键状态为当前状态
            }
        }

        // 右键处理
        if (currRight != lastRightState) {
            if (now - lastRightDebounce > 20) {
                lastRightDebounce = now;
                if (currRight == TOUCH_PRESSED) {// 右键按下，发送鼠标右键按下事件
                    BleMouseDevice.press(MOUSE_RIGHT);// 发送右键按下事件
                } else {
                    BleMouseDevice.release(MOUSE_RIGHT);// 右键松开，发送鼠标右键释放事件
                }
                lastRightState = currRight; // 更新上次右键状态为当前状态
            }
        }
    } 
    // ----------------------------------------------------
    // 情况 B：滚轮锁定模式 (is_airmouse_locked) -> 触摸键控上下顺滑滚动
    // ----------------------------------------------------
    else {
        // --- 1. 左键（向上滚动）触摸检测 ---
        if (currLeft != lastLeftState) {  // 检测左键状态变化
            if (now - lastLeftDebounce > 20) { // 去抖动处理，20ms 内忽略状态变化
                lastLeftDebounce = now;
                lastLeftState = currLeft;
                if (currLeft == TOUCH_PRESSED) { // 左键按下，发送鼠标滚轮向上事件
                    // 触摸瞬间立刻响应 1 格首包（零延迟触感）
                    BleMouseDevice.move(0, 0, 1); // 发送滚轮向上事件
                    g_scroll_dir = 1;// 设置滚轮方向为向上
                    mouseWheelCount += 1;// 滚轮计数器增加
                    scroll_accumulator_up = 0.0f; // 重置向上滚动累加器
                } else {
                    // 松开手指立刻刹车清零
                    g_scroll_dir = 0; // 设置滚轮方向为静止
                    scroll_accumulator_up = 0.0f;// 重置向上滚动累加器
                }
            }
        }

        // --- 2. 右键（向下滚动）触摸检测 ---
        if (currRight != lastRightState) {  // 检测右键状态变化
            if (now - lastRightDebounce > 20) {
                lastRightDebounce = now;
                lastRightState = currRight;
                if (currRight == TOUCH_PRESSED) { // 右键按下，发送鼠标滚轮向下事件
                    // 触摸瞬间立刻响应 1 格首包（零延迟触感）
                    BleMouseDevice.move(0, 0, -1);
                    g_scroll_dir = -1;
                    mouseWheelCount -= 1;
                    scroll_accumulator_down = 0.0f;
                } else {
                    // 松开手指立刻刹车清零
                    g_scroll_dir = 0;
                    scroll_accumulator_down = 0.0f;
                }
            }
        }

        // --- 3. 连续触摸持续按住：高帧率平滑动力学连发 (20ms 刷新周期) ---
        if (now - lastWheelTickTime >= 20) { // 每 20ms 更新一次滚轮平滑动力学
            lastWheelTickTime = now; // 更新时间戳

            // 根据滚轮灵敏度 (10% ~ 100%) 计算每 20ms 的速度增量
            // 10% 灵敏度 -> 速度 0.12 (约 160ms 滚 1 格，平缓细腻)
            // 50% 灵敏度 -> 速度 0.35 (约 57ms 滚 1 格，标准顺滑)
            // 100% 灵敏度 -> 速度 0.85 (约 23ms 滚 1 格，高速滑动)
            float speed = 0.05f + ((float)global_wheel_sens_percent / 100.0f) * 0.80f; // 计算滚轮速度增量 (0.05 ~ 0.85)

            // 左键持续触摸：向上平滑累加
            if (currLeft == TOUCH_PRESSED && lastLeftState == TOUCH_PRESSED) { // 持续按住左键
                scroll_accumulator_up += speed;  // 累加向上滚动增量
                if (scroll_accumulator_up >= 1.0f) { // 当累加器达到 1.0 时，发送滚轮向上事件
                    int8_t step = (int8_t)scroll_accumulator_up; // 取整为滚轮步数
                    scroll_accumulator_up -= step; // 减去已发送的步数，保留剩余的累加器
                    BleMouseDevice.move(0, 0, step); // 发送滚轮向上事件
                    mouseWheelCount += step;    // 更新滚轮计数器
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
static void updateLockKeyLogic() { // 锁定/断控切换逻辑
    static bool lastLockBtnState = TOUCH_RELEASED; // 上次触摸按键状态
    static unsigned long lockBtnDebounceTime = 0; // 上次触摸按键去抖动时间戳 (毫秒)
    bool currentLockState = digitalRead(PIN_M1_PAUSE); // 当前触摸按键状态 
    
    if (currentLockState != lastLockBtnState) { // 检测触摸按键状态变化
        if (millis() - lockBtnDebounceTime > 50) {   // 去抖动处理，50ms 内忽略状态变化
            lockBtnDebounceTime = millis(); // 更新时间戳
            // 触摸按下瞬间 (LOW -> HIGH)
            if (currentLockState == TOUCH_PRESSED) {  // 触摸按键按下，切换锁定状态
                is_airmouse_locked = !is_airmouse_locked;  // 切换飞鼠锁定状态
                
                // 重置滚轮计数、累加器与光标滤波残余
                g_scroll_dir = 0;  // 重置滚轮方向为静止
                mouseWheelCount = 0;  // 重置滚轮计数器
                scroll_accumulator_up = 0.0f;  // 重置向上滚动累加器
                scroll_accumulator_down = 0.0f;  // 重置向下滚动累加器
                smooth_dx = 0;  // 重置光标平滑滤波器 X 轴残余
                smooth_dy = 0;  // 重置光标平滑滤波器 Y 轴残余

                BleMouseDevice.release(MOUSE_LEFT); // 释放左键，防止锁定模式下误触发
                BleMouseDevice.release(MOUSE_RIGHT); // 释放右键，防止锁定模式下误触发

                Serial.printf("🖱️ [M1] 状态切换 -> 滚轮锁定模式: %s\r\n", is_airmouse_locked ? "开启" : "关闭"); // 打印状态切换信息
            }
            lastLockBtnState = currentLockState; // 更新上次触摸按键状态
        }
    }
}

// ====================================================
// 🎯 灵敏度调节逻辑 (GPIO 3 触摸按键)
// ====================================================
static void updateSensKeyLogic() { // 灵敏度调节逻辑
    static bool lastSensBtnState = TOUCH_RELEASED; // 上次触摸按键状态
    static unsigned long sensBtnPressTime = 0; // 触摸按键按下时间戳 (毫秒)
    static unsigned long lastAutoStepTime = 0; // 上次自动步进时间戳 (毫秒)
    static bool isLongPressed = false; // 是否为长按状态标志
    bool currentSensState = digitalRead(PIN_M1_SENS); // 当前触摸按键状态

    // 触摸按下瞬间 (LOW -> HIGH)
    if (currentSensState == TOUCH_PRESSED && lastSensBtnState == TOUCH_RELEASED) { // 检测到触摸按键按下
        sensBtnPressTime = millis(); // 记录按下时间戳
        isLongPressed = false; // 重置长按状态标志
        delay(5);  // 延时 5ms，确保按键状态稳定
    }
    
    // 持续触摸 (按住)
    if (currentSensState == TOUCH_PRESSED) {  // 检测到触摸按键持续按住
        if (millis() - sensBtnPressTime >= 400) {  // 长按 400ms 后，进入自动步进模式
            isLongPressed = true;  // 设置长按状态标志
            if (millis() - lastAutoStepTime >= 150) {  // 每 150ms 自动步进一次灵敏度
                lastAutoStepTime = millis(); // 更新时间戳 
                if (!is_airmouse_locked) { // 飞鼠模式下，调节飞鼠灵敏度
                    global_sens_percent += 10; // 每次增加 10%
                    if (global_sens_percent > 100) global_sens_percent = 10; // 超过 100% 后回绕到 10%
                    g_cfg.air_dpi_level = global_sens_percent / 10; // 更新配置
                } else { // 触摸模式下，调节触摸灵敏度
                    global_wheel_sens_percent += 10; // 每次增加 10%
                    if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10; // 超过 100% 后回绕到 10%
                    g_cfg.touch_dpi_level = global_wheel_sens_percent / 10; // 更新配置
                }
                saveConfigToNVS(); // 保存配置到 NVS
            }
        }
    }
    
    // 触摸松开瞬间 (HIGH -> LOW)
    if (currentSensState == TOUCH_RELEASED && lastSensBtnState == TOUCH_PRESSED) { // 检测到触摸按键松开
        if (!isLongPressed && (millis() - sensBtnPressTime > 25)) { // 非长按状态，且按下时间超过 25ms
            if (!is_airmouse_locked) { // 飞鼠模式下，调节飞鼠灵敏度
                global_sens_percent += 10; // 每次增加 10%
                if (global_sens_percent > 100) global_sens_percent = 10;    // 超过 100% 后回绕到 10%
                g_cfg.air_dpi_level = global_sens_percent / 10; // 更新配置
            } else {
                global_wheel_sens_percent += 10; // 每次增加 10%
                if (global_wheel_sens_percent > 100) global_wheel_sens_percent = 10;    // 超过 100% 后回绕到 10%
                g_cfg.touch_dpi_level = global_wheel_sens_percent / 10; // 更新配置
            }
            saveConfigToNVS();
        }
    }
    lastSensBtnState = currentSensState;      // 更新上次触摸按键状态
}

// ====================================================
// 🎯 初始化与主更新循环
// ====================================================
void initMouseMode() { // 初始化飞鼠模式
    is_airmouse_locked = false;   // 默认飞鼠模式为非锁定模式
    mouseWheelCount = 0; // 重置滚轮计数器
    scroll_accumulator_up = 0.0f; // 重置向上滚动累加器
    scroll_accumulator_down = 0.0f;// 重置向下滚动累加器
    smooth_dx = 0; // 重置光标平滑滤波器 X 轴残余
    smooth_dy = 0; // 重置光标平滑滤波器 Y 轴残余
    
    // 触摸模块输出高电平，采用标准 INPUT 模式
    pinMode(PIN_M1_LEFT, INPUT);    // 左键
    pinMode(PIN_M1_RIGHT, INPUT);   // 右键
    pinMode(PIN_M1_PAUSE, INPUT);   // 暂停键
    pinMode(PIN_M1_SENS, INPUT);    // 灵敏度键

    mpu6050_init();  // 初始化 MPU6050 传感器
    delay(50); // 等待 MPU6050 稳定
    gyro_offset_pitch = 0; gyro_offset_yaw = 0; // 初始化陀螺仪偏移量
    for (int i = 0; i < 50; i++) { // 采集 50 次陀螺仪数据，计算平均偏移量
        mpu6050_raw_data_t temp_data; // 临时存储陀螺仪原始数据
        mpu6050_read_raw(&temp_data); // 读取陀螺仪原始数据 
        int16_t *ptr = (int16_t *)&temp_data; // 将结构体指针转换为 int16_t 指针，方便访问各轴数据
        gyro_offset_pitch += ptr[5]; gyro_offset_yaw += ptr[6];    // 累加陀螺仪俯仰角和偏航角数据
        delay(5); // 延时 5ms，确保数据稳定
    }
    gyro_offset_pitch /= 50; gyro_offset_yaw /= 50; // 计算陀螺仪俯仰角和偏航角的平均偏移量
    Serial.println("[M1] 触摸按键版飞鼠模式初始化完成。");
}

void updateMouseMode() { // 飞鼠模式主循环更新函数
    if (!BleMouseDevice.isConnected()) return; // 如果 BLE 鼠标未连接，则直接返回，不进行后续处理
    
    updateLockKeyLogic();    // 更新锁定/断控切换逻辑
    updateSensKeyLogic();    // 更新灵敏度调节逻辑
    updateNormalButtons();   // 更新按键与滚轮核心分流逻辑

    if (!is_airmouse_locked) { // 非滚轮锁定模式下，处理飞鼠光标移动
        unsigned long now = millis(); // 获取当前时间戳 (毫秒)
        if (now - lastBleTime >= 12) {  // 每 12ms 处理一次飞鼠光标移动，约 83Hz 更新频率
            lastBleTime = now; // 更新时间戳
            
            mpu6050_raw_data_t my_mpu_data; // 创建 MPU6050 原始数据结构体实例
            mpu6050_read_raw(&my_mpu_data);  // 读取 MPU6050 原始数据
            int16_t *data_ptr = (int16_t *)&my_mpu_data; // 将结构体指针转换为 int16_t 指针，方便访问各轴数据
            
            int16_t cal_pitch = data_ptr[5] - gyro_offset_pitch; // 计算校准后的俯仰角 (pitch)
            int16_t cal_yaw = data_ptr[6] - gyro_offset_yaw; // 计算校准后的偏航角 (yaw)
            
            int16_t dynamic_deadzone = (int16_t)(g_cfg.deadzone_px * 40);  // 动态死区阈值，基于配置的死区像素值 (deadzone_px) 乘以 40，单位为陀螺仪原始数据单位
            int16_t f_pitch = (abs(cal_pitch) > dynamic_deadzone) ? cal_pitch : 0; // 如果校准后的俯仰角绝对值大于动态死区阈值，则使用校准值，否则设为 0
            int16_t f_yaw = (abs(cal_yaw) > dynamic_deadzone) ? cal_yaw : 0; // 如果校准后的偏航角绝对值大于动态死区阈值，则使用校准值，否则设为 0
            
            float target_dx = (float)f_yaw / DAMPING; // 将校准后的偏航角除以阻尼系数，得到目标 X 轴移动量
            float target_dy = (float)f_pitch / DAMPING; // 将校准后的俯仰角除以阻尼系数，得到目标 Y 轴移动量
            
            smooth_dx = smooth_dx + FILTER_ALPHA * (target_dx - smooth_dx); // 应用平滑滤波器，更新 X 轴移动量
            smooth_dy = smooth_dy + FILTER_ALPHA * (target_dy - smooth_dy); // 应用平滑滤波器，更新 Y 轴移动量
            
            int16_t move_x = (int16_t)(smooth_dx * g_cfg.air_gain); // 将平滑后的 X 轴移动量乘以飞鼠增益，得到最终 X 轴移动量
            int16_t move_y = (int16_t)(smooth_dy * g_cfg.air_gain); // 将平滑后的 Y 轴移动量乘以飞鼠增益，得到最终 Y 轴移动量
            
            if (abs(move_x) <= g_cfg.deadzone_px) move_x = 0; // 如果最终 X 轴移动量绝对值小于等于配置的死区像素值，则设为 0
            if (abs(move_y) <= g_cfg.deadzone_px) move_y = 0; // 如果最终 Y 轴移动量绝对值小于等于配置的死区像素值，则设为 0
            
            if (move_x != 0 || move_y != 0) { // 如果最终移动量不为 0，则发送鼠标移动事件
                BleMouseDevice.move(move_x, move_y, 0); // 发送鼠标移动事件，第三个参数为滚轮移动量，这里为 0
            }
            oled_mouse_x = move_x; oled_mouse_y = move_y; // 更新供 OLED 显示使用的飞鼠光标移动量
        }
    } else { // 滚轮锁定模式下，飞鼠光标不移动，重置光标坐标
        oled_mouse_x = 0; oled_mouse_y = 0; // 重置供 OLED 显示使用的飞鼠光标移动量
    }
}