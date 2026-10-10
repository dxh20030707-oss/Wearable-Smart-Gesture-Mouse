#include "touch_mode.h"  
#include "config.h"
#include "display_mode.h" 
#include <BleMouse.h> 
#include <Arduino.h>
#include <Wire.h>

extern BleMouse BleMouseDevice; //  from ble_mouse.h
extern SystemConfig g_cfg; //  from config.h

int16_t oled_touch_x = 0;   // 触控板第一个触点的 X 坐标
int16_t oled_touch_y = 0;    // 触控板第一个触点的 Y 坐标
int16_t oled_touch2_x = 0;   // 触控板第二个触点的 X 坐标   
int16_t oled_touch2_y = 0;     // 触控板第二个触点的 Y 坐标
uint8_t oled_touch_points = 0;  // 触控板当前检测到的触点数量 (0, 1, or 2)
uint8_t oled_click_count = 0;// 触控板触点的点击次数

#define FT6336U_I2C_ADDR    0x38   // FT6336U I2C 地址
#define FT_REG_TD_STATUS    0x02  // FT6336U 触点状态寄存器地址
#define FT_REG_P1_XH        0x03  // FT6336U 第一个触点的 X 坐标高字节寄存器地址
#define FT_REG_P2_XH        0x09  // FT6336U 第二个触点的 X 坐标高字节寄存器地址

volatile bool ft_interrupt_flag = false; // 标记 FT6336U 触控中断事件发生
void IRAM_ATTR handleTouchINT_ISR() { // 中断服务程序，标记触控中断事件
    ft_interrupt_flag = true; 
}

static bool readFT6336U_Dual(int16_t &x1, int16_t &y1, int16_t &x2, int16_t &y2, uint8_t &points) { // 读取 FT6336U 双触点数据
    Wire.beginTransmission(FT6336U_I2C_ADDR);// 向 FT6336U 发送 I2C 地址
    Wire.write(FT_REG_TD_STATUS); // 请求读取触点状态寄存器
    if (Wire.endTransmission() != 0) return false;  // 检查 I2C 传输是否成功

    Wire.requestFrom(FT6336U_I2C_ADDR, 1); // 请求读取一个字节的数据
    if (!Wire.available()) return false; // 检查是否有数据可用
    points = Wire.read() & 0x0F;  // 读取触点数量 (0, 1, or 2)

    if (points == 0) {  // 没有触点，重置坐标
        x1 = 0; y1 = 0; x2 = 0; y2 = 0; // 重置坐标
        return true; // 返回成功
    }

    if (points == 1) {  // 只有一个触点，读取第一个触点的坐标
        Wire.beginTransmission(FT6336U_I2C_ADDR);  // 向 FT6336U 发送 I2C 地址
        Wire.write(FT_REG_P1_XH);   // 请求读取第一个触点的 X 坐标高字节寄存器
        if (Wire.endTransmission() != 0) return false; // 检查 I2C 传输是否成功

        Wire.requestFrom(FT6336U_I2C_ADDR, 4);  // 请求读取 4 个字节的数据 (X 高字节, X 低字节, Y 高字节, Y 低字节)
        if (Wire.available() == 4) {  // 检查是否有 4 个字节的数据可用
            uint8_t xh = Wire.read(); uint8_t xl = Wire.read();  // 读取第一个触点的 X 坐标高字节和低字节
            uint8_t yh = Wire.read(); uint8_t yl = Wire.read();         // 读取第一个触点的 Y 坐标高字节和低字节
            x1 = ((xh & 0x0F) << 8) | xl; // 计算第一个触点的 X 坐标
            y1 = ((yh & 0x0F) << 8) | yl; // 计算第一个触点的 Y 坐标
            x2 = 0; y2 = 0; 
            return true;
        }
    } 
    else if (points == 2) { // 有两个触点，读取两个触点的坐标
        Wire.beginTransmission(FT6336U_I2C_ADDR); // 向 FT6336U 发送 I2C 地址
        Wire.write(FT_REG_P1_XH); // 请求读取第一个触点的 X 坐标高字节寄存器
        if (Wire.endTransmission() != 0) return false; // 检查 I2C 传输是否成功

        Wire.requestFrom(FT6336U_I2C_ADDR, 10); // 请求读取 10 个字节的数据 (第一个触点的 X/Y 坐标 + 第二个触点的 X/Y 坐标)
        if (Wire.available() == 10) {  // 检查是否有 10 个字节的数据可用
            uint8_t x1h = Wire.read(); uint8_t x1l = Wire.read();
            uint8_t y1h = Wire.read(); uint8_t y1l = Wire.read();
            x1 = ((x1h & 0x0F) << 8) | x1l;
            y1 = ((y1h & 0x0F) << 8) | y1l;

            Wire.read(); Wire.read();  // 读取第一个触点的压力和触摸面积寄存器 (忽略)

            uint8_t x2h = Wire.read(); uint8_t x2l = Wire.read();
            uint8_t y2h = Wire.read(); uint8_t y2l = Wire.read(); 
            x2 = ((x2h & 0x0F) << 8) | x2l;
            y2 = ((y2h & 0x0F) << 8) | y2l;
            return true;
        }
    }
    return false;
}

static bool isTouching = false; // 标记当前是否有触控点按下
static uint8_t lastPoints = 0; // 上一次检测到的触控点数量
static int16_t startTouchX = 0, startTouchY = 0; // 记录触控开始时的第一个触点坐标
static int16_t lastTouchX = 0, lastTouchY = 0; // 记录上一次检测到的第一个触点坐标
static int16_t lastP1Y = 0, lastP2Y = 0; // 记录上一次检测到的两个触点的 Y 坐标
static uint8_t clickCount = 0; // 记录连续点击次数  
static unsigned long touchStartTime = 0; // 记录触控开始时间
static unsigned long touchReleaseTime = 0; // 记录触控释放时间
static bool hasMoved = false; // 标记触控过程中是否发生了移动
static unsigned long dualTouchExitTime = 0; // 双指操作退出时间
static bool wasDualScrolling = false;  // 标记是否刚刚结束了双指滚动操作
static int16_t lastTouchDist = 0;  // 记录上一次双指触控的距离，用于计算缩放手势
static bool isDragging = false;  // 标记当前是否处于拖拽状态

static unsigned long dualTouchStartTime = 0; // 记录双指触控开始时间
static uint8_t maxPointsInCurrentGesture = 0; // 记录当前手势中检测到的最大触控点数量 (用于区分单指和双指操作)
static unsigned long lastTouchMouseSendTime = 0; // 记录上一次发送鼠标移动事件的时间，用于节流鼠标移动事件的发送频率

static bool isDualFingerMoving = false;  // 标记当前是否处于双指移动状态 (用于区分双指缩放和双指滚动)

static int16_t dualStartP1Y = 0;    // 记录双指操作开始时第一个触点的 Y 坐标       
static int16_t dualStartP2Y = 0;      // 记录双指操作开始时第二个触点的 Y 坐标    
static unsigned long lastAutoScrollTime = 0;   // 记录上一次自动滚动事件的时间，用于节流自动滚动事件的发送频率

#define SLIDE_THRESHOLD     10    // 触控滑动阈值，用于判断是否发生了滑动操作
#define TIMEOUT_WINDOW      250   // 触控点击时间窗口，单位毫秒，用于判断连续点击次数
#define ZOOM_THRESHOLD      6     // 双指缩放阈值，用于判断是否发生了缩放操作
#define COOLING_DOWN_MS     150  // 双指滚动操作后的冷却时间，单位毫秒，用于避免误触发单指操作

#define AUTO_SCROLL_THRESHOLD  20  // 双指滚动阈值，用于判断是否发生了双指滚动操作
#define AUTO_SCROLL_INTERVAL   40  // 双指滚动事件发送间隔，单位毫秒，用于节流双指滚动事件的发送频率

void initTouchMode() {  // 初始化 FT6336U 触控板


    pinMode(PIN_M2_RST, OUTPUT); // 设置 FT6336U 复位引脚为输出模式
    digitalWrite(PIN_M2_RST, HIGH);  // 先拉高复位引脚，确保 FT6336U 处于非复位状态
    delay(10); // 延迟一段时间，等待 FT6336U 复位完成
    digitalWrite(PIN_M2_RST, LOW);   // 拉低复位引脚，使 FT6336U 进入复位状态
    delay(20); // 延迟一段时间，等待 FT6336U 复位完成
    digitalWrite(PIN_M2_RST, HIGH);  // 拉高复位引脚，使 FT6336U 退出复位状态
    delay(200);  // 延迟一段时间，等待 FT6336U 初始化完成
    
    pinMode(PIN_M2_INT, INPUT_PULLUP);  // 设置 FT6336U 中断引脚为输入模式，并启用上拉电阻
    ft_interrupt_flag = false;  // 清除中断标志位
    attachInterrupt(digitalPinToInterrupt(PIN_M2_INT), handleTouchINT_ISR, CHANGE);  // 绑定中断服务程序，当 FT6336U 中断引脚状态发生变化时触发中断

    wasDualScrolling = false; // 初始化双指滚动状态
    dualTouchExitTime = 0; // 初始化双指操作退出时间
    lastTouchDist = 0;  // 初始化上一次双指触控的距离
    isDragging = false;   // 初始化拖拽状态
    maxPointsInCurrentGesture = 0;  // 初始化当前手势中检测到的最大触控点数量
    lastTouchMouseSendTime = 0;  // 初始化上一次发送鼠标移动事件的时间
    isDualFingerMoving = false; // 初始化双指移动状态
    lastAutoScrollTime = 0; // 初始化上一次自动滚动事件的时间

    Serial.println("[M2] FT6336U 触控板驱动就绪。");
}

void updateTouchMode() {  // 更新触控板状态并处理鼠标事件
    if (!BleMouseDevice.isConnected()) return;  // 如果蓝牙鼠标未连接，则不处理触控事件

    int16_t p1x = 0, p1y = 0, p2x = 0, p2y = 0;  // 定义两个触点的坐标变量
    uint8_t touch_points = 0; // 定义触控点数量变量

    if (ft_interrupt_flag || isTouching) { // 如果触控中断标志位被设置，或者当前有触控点按下，则读取触控数据
        ft_interrupt_flag = false;  // 清除中断标志位

        bool read_success = readFT6336U_Dual(p1x, p1y, p2x, p2y, touch_points); // 读取 FT6336U 双触点数据
        
        if (read_success) {  // 如果读取成功，则处理触控数据
            uint8_t actual_physical_points = touch_points; // 记录实际检测到的触控点数量 (0, 1, or 2) 

            if (touch_points > 0 && actual_physical_points > 0) { // 如果有触控点按下，则更新 OLED 显示的触控点坐标和数量
                oled_touch_points = touch_points; // 更新 OLED 显示的触控点数量
                oled_touch_x = p1x; oled_touch_y = p1y; // 更新 OLED 显示的第一个触点坐标
                oled_touch2_x = p2x; oled_touch2_y = p2y; // 更新 OLED 显示的第二个触点坐标
            }

            if (touch_points > maxPointsInCurrentGesture) { // 更新当前手势中检测到的最大触控点数量
                maxPointsInCurrentGesture = touch_points; // 更新当前手势中检测到的最大触控点数量
            }

            if (!isTouching && touch_points > 0 && actual_physical_points > 0) { // 如果当前没有新的触控点按下，但有触控点按下，则记录触控开始时间
                isTouching = true;  // 标记当前有触控点按下
                touchStartTime = millis();// 记录触控开始时间
                hasMoved = false; // 标记触控过程中是否发生了移动
                isDualFingerMoving = false;  // 标记当前是否处于双指移动状态 (用于区分双指缩放和双指滚动)
                startTouchX = p1x; startTouchY = p1y; // 记录触控开始时的第一个触点坐标
                lastTouchX = p1x;  lastTouchY = p1y;    // 记录上一次检测到的第一个触点坐标
                lastP1Y = p1y;     lastP2Y = p2y; // 记录上一次检测到的两个触点的 Y 坐标
                lastPoints = touch_points; // 记录上一次检测到的触控点数量
                maxPointsInCurrentGesture = touch_points;  // 记录当前手势中检测到的最大触控点数量
                
                if (touch_points == 2) {  // 如果当前有两个触控点按下，则记录双指操作开始时间和初始 Y 坐标
                    dualTouchStartTime = millis();  // 记录双指操作开始时间
                    dualStartP1Y = p1y;   // 记录双指操作开始时第一个触点的 Y 坐标
                    dualStartP2Y = p2y;  // 记录双指操作开始时第二个触点的 Y 坐标
                    lastAutoScrollTime = millis(); // 记录上一次自动滚动事件的时间
                }

                if (clickCount == 1 && (touchStartTime - touchReleaseTime < TIMEOUT_WINDOW)) {  // 如果在时间窗口内连续点击，则增加点击计数器

                } else { // 如果当前触控点数量为1，则重置点击计数器
                    clickCount = 0; // 重置点击计数器
                }
            } 
            else if (isTouching && touch_points > 0) { // 如果当前有触控点按下，则处理触控移动和双指操作
                if (lastPoints != 2 && touch_points == 2) { // 如果上一次检测到的触控点数量不是2，而当前检测到的触控点数量是2，则记录双指操作开始时间和初始 Y 坐标
                    dualTouchStartTime = millis();  // 记录双指操作开始时间
                    if (actual_physical_points == 2) { // 如果实际检测到的触控点数量是2，则记录双指操作开始时的两个触点的 Y 坐标
                        dualStartP1Y = p1y;  // 记录双指操作开始时第一个触点的 Y 坐标
                        dualStartP2Y = p2y;  // 记录双指操作开始时第二个触点的 Y 坐标
                        lastP1Y = p1y; lastP2Y = p2y; // 记录上一次检测到的两个触点的 Y 坐标
                    }
                    lastAutoScrollTime = millis(); // 记录上一次自动滚动事件的时间
                }
                if (lastPoints == 2 && touch_points == 1) { // 如果上一次检测到的触控点数量是2，而当前检测到的触控点数量是1，则记录双指操作退出时间，并标记刚刚结束了双指滚动操作
                    dualTouchExitTime = millis();  // 记录双指操作退出时间
                    wasDualScrolling = true;     // 标记刚刚结束了双指滚动操作 
                }

                // A. 单指位移控光标
                if (touch_points == 1) { // 如果当前有一个触控点按下，则处理单指位移控光标
                    if (wasDualScrolling && (millis() - dualTouchExitTime < COOLING_DOWN_MS)) { // 如果刚刚结束了双指滚动操作，并且距离双指操作退出时间小于冷却时间，则不处理单指位移控光标
                        if (actual_physical_points > 0) { // 如果实际检测到的触控点数量大于0，则更新上一次检测到的第一个触点坐标
                            lastTouchX = p1x; lastTouchY = p1y;  // 更新上一次检测到的第一个触点坐标
                        }
                    } 
                    else if (lastPoints == 1 && actual_physical_points > 0) {  // 如果上一次检测到的触控点数量是1，并且实际检测到的触控点数量大于0，则处理单指位移控光标
                        int16_t deltaX = p1x - lastTouchX;  // 计算当前触控点的 X 坐标与上一次检测到的第一个触点的 X 坐标的差值
                        int16_t deltaY = p1y - lastTouchY;  // 计算当前触控点的 Y 坐标与上一次检测到的第一个触点的 Y 坐标的差值

                        if (!hasMoved && (abs(p1x - startTouchX) > SLIDE_THRESHOLD || abs(p1y - startTouchY) > SLIDE_THRESHOLD)) {  // 如果当前触控点的坐标与触控开始时的第一个触点的坐标的差值超过滑动阈值，则标记触控过程中发生了移动，并按下鼠标左键
                            hasMoved = true;  // 标记触控过程中发生了移动
                            if (clickCount == 1 && (p1y - startTouchY) > SLIDE_THRESHOLD) {  // 如果在时间窗口内连续点击，并且当前触控点的 Y 坐标与触控开始时的第一个触点的 Y 坐标的差值超过滑动阈值，则标记当前处于拖拽状态，并按下鼠标左键
                                isDragging = true;   // 标记当前处于拖拽状态
                                BleMouseDevice.press(MOUSE_LEFT);  // 按下鼠标左键
                                clickCount = 0;  // 重置点击计数器
                            }
                        }

                        if (hasMoved && (abs(deltaX) > g_cfg.deadzone_px || abs(deltaY) > g_cfg.deadzone_px)) { // 如果触控过程中发生了移动，并且当前触控点的坐标与上一次检测到的第一个触点的坐标的差值超过死区阈值，则发送鼠标移动事件
                            int16_t move_x = (int16_t)(deltaX * g_cfg.touch_gain); // 计算鼠标移动的 X 坐标增量
                            int16_t move_y = (int16_t)(deltaY * g_cfg.touch_gain); // 计算鼠标移动的 Y 坐标增量
                            
                            if (millis() - lastTouchMouseSendTime >= 12) {  // 如果距离上一次发送鼠标移动事件的时间超过12毫秒，则发送鼠标移动事件
                                lastTouchMouseSendTime = millis(); // 记录上一次发送鼠标移动事件的时间
                                BleMouseDevice.move(move_x, move_y, 0);  // 发送鼠标移动事件
                                lastTouchX = p1x;  // 记录上一次检测到的第一个触点的 X 坐标
                                lastTouchY = p1y;  // 记录上一次检测到的第一个触点的 Y 坐标
                            }
                        }
                    }
                }
                // B. 双指不松手滑动滚动
                else if (touch_points == 2) {  // 如果当前有两个触控点按下，则处理双指不松手滑动滚动
                    if (lastPoints != 2) {  // 如果上一次检测到的触控点数量不是2，则记录双指操作开始时间和初始 Y 坐标，并标记刚刚结束了双指滚动操作
                        if (actual_physical_points == 2) {    // 如果实际检测到的触控点数量是2，则记录双指操作开始时的两个触点的 Y 坐标
                            dualStartP1Y = p1y;  // 记录双指操作开始时第一个触点的 Y 坐标
                            dualStartP2Y = p2y;  // 记录双指操作开始时第二个触点的 Y 坐标
                            lastP1Y = p1y; lastP2Y = p2y;  // 记录上一次检测到的两个触点的 Y 坐标
                            lastTouchDist = sqrt(pow(p1x - p2x, 2) + pow(p1y - p2y, 2)); // 记录上一次双指触控的距离，用于计算缩放手势
                        }
                        hasMoved = true; wasDualScrolling = true;  // 标记触控过程中发生了移动，并标记刚刚结束了双指滚动操作
                        lastAutoScrollTime = millis();  // 记录上一次自动滚动事件的时间
                    }

                    if (actual_physical_points == 2) {  // 如果实际检测到的触控点数量是2，则计算当前双指触控的距离，并判断是否发生了缩放操作
                        int16_t currentDist = sqrt(pow(p1x - p2x, 2) + pow(p1y - p2y, 2));  // 计算当前双指触控的距离
                        int16_t distDelta = currentDist - lastTouchDist; // 计算当前双指触控的距离与上一次双指触控的距离的差值

                        if (abs(distDelta) >= ZOOM_THRESHOLD) { // 如果双指触控的距离变化超过缩放阈值，则处理缩放手势
                            isDualFingerMoving = true;  // 标记正在处理双指触控的距离变化
                            if (millis() - lastTouchMouseSendTime >= 40) { // 如果距离上一次发送鼠标滚轮事件的时间超过40毫秒，则发送鼠标滚轮事件
                                lastTouchMouseSendTime = millis();  // 记录上一次发送鼠标滚轮事件的时间
                                BleMouseDevice.press(MOUSE_MIDDLE);  // 按下鼠标中键
                                if (distDelta > 0) BleMouseDevice.move(0, 0, 1);  // 如果双指触控的距离变大，则向上滚动
                                else BleMouseDevice.move(0, 0, -1); // 如果双指触控的距离变小，则向下滚动
                                delay(5); // 延迟一段时间，确保鼠标滚轮事件发送完成
                                BleMouseDevice.release(MOUSE_MIDDLE);  // 释放鼠标中键
                            }
                            lastTouchDist = currentDist;  // 记录上一次双指触控的距离
                            lastP1Y = p1y; lastP2Y = p2y;  // 记录上一次检测到的两个触点的 Y 坐标
                        } 
                        else {
                            int16_t total_offset_p1 = p1y - dualStartP1Y;  // 记录第一个触点的总偏移量
                            int16_t total_offset_p2 = p2y - dualStartP2Y;  // 记录第二个触点的总偏移量
                            int16_t current_total_offset = (total_offset_p1 + total_offset_p2) / 2;  // 计算两个触点的平均总偏移量

                            if (abs(current_total_offset) >= AUTO_SCROLL_THRESHOLD) {  // 如果两个触点的平均总偏移量超过自动滚动阈值，则处理双指不松手滑动滚动
                                isDualFingerMoving = true;  // 标记正在处理双指不松手滑动滚动

                                unsigned long current_time = millis();  // 获取当前时间
                                if (current_time - lastAutoScrollTime >= AUTO_SCROLL_INTERVAL) {  // 如果距离上一次自动滚动事件的时间超过自动滚动事件发送间隔，则发送鼠标滚轮事件
                                    lastAutoScrollTime = current_time;  // 记录上一次自动滚动事件的时间

                                    int8_t scroll_dir = (current_total_offset > 0) ? -1 : 1;  // 如果两个触点的平均总偏移量大于0，则向下滚动，否则向上滚动
                                    BleMouseDevice.move(0, 0, scroll_dir);  // 发送鼠标滚轮事件
                                }
                            }
                        }
                        lastTouchDist = currentDist;  // 记录上一次双指触控的距离
                    }
                }
                
                if (actual_physical_points > 0) {  // 如果实际检测到的触控点数量大于0，则更新上一次检测到的触控点数量
                    lastPoints = touch_points;  // 更新上一次检测到的触控点数量
                }
            }
        }
        
        // C. 手指抬起判定
        if (isTouching && touch_points == 0) {  // 如果当前没有触控点按下，则处理手指抬起事件
            isTouching = false;  // 标记当前没有触控点按下
            touchReleaseTime = millis();  // 记录触控释放时间
            oled_touch_points = 0;   // 更新 OLED 显示的触控点数量为0
            
            if (isDragging) { // 如果当前处于拖拽状态，则释放鼠标左键，并标记不再处于拖拽状态
                BleMouseDevice.release(MOUSE_LEFT);   // 释放鼠标左键
                isDragging = false;  // 标记不再处于拖拽状态
                hasMoved = true;  // 标记触控过程中发生了移动
            }
            if (wasDualScrolling) hasMoved = true;  // 如果刚刚结束了双指滚动操作，则标记触控过程中发生了移动
            
            if (maxPointsInCurrentGesture == 2) {  // 如果当前手势中检测到的最大触控点数量是2，则判断是否发生了双指点击事件
                if ((touchReleaseTime - dualTouchStartTime < 300) && (!isDualFingerMoving)) { // 如果双指操作持续时间小于300毫秒，并且没有发生双指移动，则触发右键点击事件
                    BleMouseDevice.click(MOUSE_RIGHT); // 触发右键点击事件
                    clickCount = 0;  // 重置点击计数器
                    hasMoved = true;  // 标记触控过程中发生了移动
                }
            }

            if (!hasMoved && (touchReleaseTime - touchStartTime < TIMEOUT_WINDOW)) {  // 如果触控过程中没有发生移动，并且触控持续时间小于时间窗口，则增加点击计数器
                clickCount++;  // 增加点击计数器
            }
            oled_click_count = clickCount;  // 更新 OLED 显示的点击次数
            wasDualScrolling = false;  // 标记不再处于双指滚动状态  
            isDualFingerMoving = false;  // 标记不再处于双指移动状态 
            maxPointsInCurrentGesture = 0;  // 重置当前手势中检测到的最大触控点数量
        }
    }

    if (clickCount > 0 && !isTouching && (millis() - touchReleaseTime > TIMEOUT_WINDOW)) { // 如果点击计数器大于0，并且当前没有触控点按下，并且距离触控释放时间超过时间窗口，则根据点击计数器的值触发相应的鼠标点击事件
        switch (clickCount) {// 根据点击计数器的值触发相应的鼠标点击事件
            case 1: BleMouseDevice.click(MOUSE_LEFT); break;  // 触发单击事件 
            case 2: BleMouseDevice.click(MOUSE_LEFT); BleMouseDevice.click(MOUSE_LEFT); break;  // 触发双击事件
        }
        clickCount = 0; oled_click_count = 0;  // 重置点击计数器
    }
}