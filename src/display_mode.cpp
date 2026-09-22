#include "display_mode.h"
#include "config.h"
#include <Adafruit_GFX.h> // 引入 Adafruit_GFX 库以支持图形绘制
#include <Adafruit_SSD1306.h> // 引入 Adafruit_SSD1306 库以支持 OLED 显示屏
#include <Arduino.h>  // 引入 Arduino 库以支持串口通信

// 🎯 定义全局 display 实例化对象，供全局链接
Adafruit_SSD1306 display(128, 64, &Wire, -1);  // 128x64 OLED 显示屏

void initDisplayMode(void) {
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {   
        Serial.println(F("[OLED] SSD1306 初始化失败！"));
        for(;;); 
    }
    display.clearDisplay(); // 清空显示缓冲区
    display.setTextWrap(false); // 禁止自动换行
    display.display();  // 将缓冲区内容显示到屏幕上 
    Serial.println("[OLED] 驱动初始化完毕。");
} // 初始化 OLED 显示屏

// 🎯 模式 1：飞鼠 / 滚轮断控 OLED 画面刷新
void updateDisplayMode1(int16_t x, int16_t y, float sens, bool is_connected) { 
    extern uint8_t global_sens_percent;  // 飞鼠灵敏度百分比 (1 ~ 100)
    extern uint8_t global_wheel_sens_percent;  // 滚轮灵敏度百分比 (1 ~ 100)
    extern bool is_airmouse_locked;   // 飞鼠断控状态 (true = 断控，false = 正常)
    extern volatile int32_t mouseWheelCount; // 断控滚轮计数（由触摸滚动逻辑累加，供下方动画显示）

    display.clearDisplay(); // 清空显示缓冲区
    
    // 顶部状态栏 (反显)
    display.fillRect(0, 0, 128, 12, SSD1306_WHITE);  // 绘制白色矩形作为状态栏背景
    display.setTextColor(SSD1306_BLACK);  // 设置文本颜色为黑色
    display.setTextSize(1); // 设置文本大小为 1
    display.setCursor(4, 2); // 设置文本起始位置
    
    if (is_airmouse_locked) {// 如果飞鼠处于断控状态
        display.print("M1: PAUSE & SCROLL");  // 显示断控状态
    } else { // 如果飞鼠处于正常状态
        display.print("M1: AIR MOUSE");   // 显示飞鼠模式    
    }
    
    display.setCursor(95, 2); // 设置连接状态文本位置
    display.print(is_connected ? "CONN" : "DISC"); // 显示连接状态 (已连接/未连接)

    display.setTextColor(SSD1306_WHITE);  // 设置文本颜色为白色，准备绘制其他信息
    
    if (!is_airmouse_locked) { // 如果飞鼠处于正常状态
        display.setCursor(4, 20); // 设置鼠标位置文本位置
        display.printf("dX: %-4d  dY: %-4d", x, y); // 显示鼠标位置
        
        display.setCursor(4, 35); // 设置鼠标灵敏度文本位置
        display.printf("Mouse Sens: %d%%", global_sens_percent);
        
        // 动态准星雷达图
        int centerX = 104, centerY = 33; // 准星中心位置
        display.drawCircle(centerX, centerY, 8, SSD1306_WHITE); // 绘制准星外圆
        display.drawFastHLine(centerX - 12, centerY, 24, SSD1306_WHITE); // 绘制准星水平线 
        display.drawFastVLine(centerX, centerY - 12, 24, SSD1306_WHITE); // 绘制准星垂直线
        int16_t offset_x = constrain(x, -6, 6);  // 限制鼠标偏移量在 [-6, 6] 范围内
        int16_t offset_y = constrain(y, -6, 6);  // 限制鼠标偏移量在 [-6, 6] 范围内
        display.fillCircle(centerX + offset_x, centerY + offset_y, 2, SSD1306_WHITE);  // 绘制鼠标位置点
    } 
    else {
        display.setCursor(4, 20); // 设置滚轮状态文本位置
        display.print("Status: SCROLLING...");// 显示滚轮状态
        
        display.setCursor(4, 35);  // 设置滚轮灵敏度文本位置
        display.printf("Scroll Sens: %d%%", global_wheel_sens_percent); // 显示滚轮灵敏度百分比
        
        // 滚轮滚动动画
        int animX = 108, animY = 33; // 动画中心位置
        display.drawRoundRect(animX - 6, animY - 12, 12, 24, 3, SSD1306_WHITE);  // 绘制滚轮动画边框
        display.drawFastHLine(animX - 6, animY, 12, SSD1306_WHITE); // 绘制滚轮动画中间线
        
        if ((mouseWheelCount % 2) == 0) { // 根据滚轮计数决定绘制向上或向下的三角形
            display.fillTriangle(animX, animY - 8, animX - 3, animY - 3, animX + 3, animY - 3, SSD1306_WHITE);// 绘制向上滚动的三角形
        } else {
            display.fillTriangle(animX, animY + 8, animX - 3, animY + 3, animX + 3, animY + 3, SSD1306_WHITE);
        }
    }
    
    // 进度条
    int bar_x = 4, bar_y = 52, bar_w = 120, bar_h = 6;  // 进度条位置和大小
    display.drawRect(bar_x, bar_y, bar_w, bar_h, SSD1306_WHITE);  // 绘制进度条边框
    
    uint8_t current_render_percent = is_airmouse_locked ? global_wheel_sens_percent : global_sens_percent;  // 根据当前模式选择当前渲染百分比
    int fill_w = (int)((float)current_render_percent / 100.0f * (bar_w - 4)); // 计算进度条填充宽度
    display.fillRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, SSD1306_WHITE); // 绘制进度条填充
    
    display.display();  // 将缓冲区内容显示到屏幕上
}

// 🎯 模式 2：触控屏控制鼠标 OLED 画面刷新
void updateDisplayMode2(int16_t x, int16_t y, float sens, bool is_connected) {
    extern int16_t oled_touch2_x;
    extern int16_t oled_touch2_y;
    extern uint8_t oled_touch_points; 
    extern uint8_t oled_click_count;   
    extern uint8_t global_sens_percent; 
    
    display.clearDisplay();
    
    // 顶部状态栏
    display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(4, 2);
    if (oled_touch_points == 2) {
        display.print("M2: DUAL-TOUCH"); 
    } else {
        display.print("M2: TOUCH PAD");
    }
    
    display.setCursor(95, 2);
    display.print(is_connected ? "CONN" : "DISC");

    display.setTextColor(SSD1306_WHITE);
    
    if (oled_touch_points == 2) {
        display.setCursor(4, 16);
        display.printf("P1 X:%-3d Y:%-3d", x, y);
        display.setCursor(4, 27);
        display.printf("P2 X:%-3d Y:%-3d", oled_touch2_x, oled_touch2_y);
        
        int16_t dist = sqrt(pow(x - oled_touch2_x, 2) + pow(y - oled_touch2_y, 2));
        display.setCursor(4, 38);
        display.printf("Dist: %d px", dist);
    } else {
        display.setCursor(4, 18);
        display.printf("X: %-3d  Y: %-3d", x, y);
        
        display.setCursor(4, 32);
        display.print("Click Que: ");
        for(int i = 0; i < oled_click_count; i++) {
            display.fillCircle(68 + (i * 10), 35, 3, SSD1306_WHITE); 
        }
        if(oled_click_count == 0) display.print("none");
    }

    display.setCursor(4, 49);
    display.printf("Sens Level: %d%%", global_sens_percent);

    // 右侧：模拟触摸视口
    int box_x = 92, box_y = 16, box_w = 34, box_h = 44;
    display.drawRect(box_x, box_y, box_w, box_h, SSD1306_WHITE);
    
    for(int i = box_x + 2; i < box_x + box_w; i += 4) display.drawPixel(i, box_y + (box_h / 2), SSD1306_WHITE);
    for(int j = box_y + 2; j < box_y + box_h; j += 4) display.drawPixel(box_x + (box_w / 2), j, SSD1306_WHITE);

    int m1_x = box_x + 2 + (abs(x) % (box_w - 4));
    int m1_y = box_y + 2 + (abs(y) % (box_h - 4));
    display.drawFastHLine(m1_x - 2, m1_y, 5, SSD1306_WHITE);
    display.drawFastVLine(m1_x, m1_y - 2, 5, SSD1306_WHITE);

    if (oled_touch_points == 2) {
        int m2_x = box_x + 2 + (abs(oled_touch2_x) % (box_w - 4));
        int m2_y = box_y + 2 + (abs(oled_touch2_y) % (box_h - 4));
        
        display.drawFastHLine(m2_x - 2, m2_y, 5, SSD1306_WHITE);
        display.drawFastVLine(m2_x, m2_y - 2, 5, SSD1306_WHITE);
        display.drawPixel((m1_x + m2_x) / 2, (m1_y + m2_y) / 2, SSD1306_WHITE);
    }

    display.display();
}

// 🎯 旋钮/滚轮独立配置界面
void drawWheelConfigUI(bool is_downsliding, bool is_upsliding, float wheel_sens, bool is_connected) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    
    display.fillRoundRect(0, 0, 128, 14, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(4, 3);
    display.print("SCROLL CONFIG"); 
    
    display.setCursor(90, 3);
    if (is_connected) display.print("BLE:ON"); else display.print("BLE:--");
    
    display.setTextColor(SSD1306_WHITE);
    display.drawFastHLine(0, 16, 128, SSD1306_WHITE); 
    
    display.setCursor(4, 23);
    display.print("Status: ");
    display.setCursor(52, 23);
    
    if (is_downsliding) {
        display.fillTriangle(115, 22, 110, 29, 120, 29, SSD1306_WHITE); 
        display.print("DOWN SCROLLING..."); 
    } else if (is_upsliding) {
        display.fillTriangle(115, 29, 110, 22, 120, 22, SSD1306_WHITE); 
        display.print("UP SCROLLING...");
    } else {
        display.drawCircle(115, 25, 3, SSD1306_WHITE); 
        display.print("LOCKED / IDLE");
    }

    display.setCursor(4, 38);
    display.print("Scroll Sens: x");
    display.print(wheel_sens, 1);
    
    int bar_x = 4, bar_y = 50, bar_w = 120, bar_h = 7;
    display.drawRect(bar_x, bar_y, bar_w, bar_h, SSD1306_WHITE);
    
    float percent = (wheel_sens - 1.0f) / 3.0f; 
    int fill_w = (int)(percent * (bar_w - 4));
    if (fill_w < 1 && wheel_sens > 0) fill_w = 4; 
    
    display.fillRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, SSD1306_WHITE);
    display.display(); 
}