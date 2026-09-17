#include "display_mode.h"
#include "config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>

// 🎯 定义全局 display 实例化对象，供全局链接
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void initDisplayMode(void) {
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("[OLED] SSD1306 初始化失败！"));
        for(;;); 
    }
    display.clearDisplay();
    display.setTextWrap(false); 
    display.display();
    Serial.println("[OLED] 驱动初始化完毕。");
}

// 🎯 模式 1：飞鼠 / 滚轮断控 OLED 画面刷新
void updateDisplayMode1(int16_t x, int16_t y, float sens, bool is_connected) {
    extern uint8_t global_sens_percent;
    extern uint8_t global_wheel_sens_percent;
    extern bool is_airmouse_locked; 
    extern volatile int32_t mouseWheelCount; // 实时获取 EC11 滚轮编码器物理计数

    display.clearDisplay();
    
    // 顶部状态栏 (反显)
    display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(4, 2);
    
    if (is_airmouse_locked) {
        display.print("M1: PAUSE & SCROLL"); 
    } else {
        display.print("M1: AIR MOUSE");      
    }
    
    display.setCursor(95, 2);
    display.print(is_connected ? "CONN" : "DISC");

    display.setTextColor(SSD1306_WHITE);
    
    if (!is_airmouse_locked) {
        display.setCursor(4, 20);
        display.printf("dX: %-4d  dY: %-4d", x, y);
        
        display.setCursor(4, 35);
        display.printf("Mouse Sens: %d%%", global_sens_percent);
        
        // 动态准星雷达图
        int centerX = 104, centerY = 33;
        display.drawCircle(centerX, centerY, 8, SSD1306_WHITE);
        display.drawFastHLine(centerX - 12, centerY, 24, SSD1306_WHITE);
        display.drawFastVLine(centerX, centerY - 12, 24, SSD1306_WHITE);
        int16_t offset_x = constrain(x, -6, 6);
        int16_t offset_y = constrain(y, -6, 6);
        display.fillCircle(centerX + offset_x, centerY + offset_y, 2, SSD1306_WHITE);
    } 
    else {
        display.setCursor(4, 20);
        display.print("Status: SCROLLING...");
        
        display.setCursor(4, 35);
        display.printf("Scroll Sens: %d%%", global_wheel_sens_percent);
        
        // EC11 滚轮动画
        int animX = 108, animY = 33;
        display.drawRoundRect(animX - 6, animY - 12, 12, 24, 3, SSD1306_WHITE);
        display.drawFastHLine(animX - 6, animY, 12, SSD1306_WHITE);
        
        if ((mouseWheelCount % 2) == 0) {
            display.fillTriangle(animX, animY - 8, animX - 3, animY - 3, animX + 3, animY - 3, SSD1306_WHITE);
        } else {
            display.fillTriangle(animX, animY + 8, animX - 3, animY + 3, animX + 3, animY + 3, SSD1306_WHITE);
        }
    }
    
    // 进度条
    int bar_x = 4, bar_y = 52, bar_w = 120, bar_h = 6;
    display.drawRect(bar_x, bar_y, bar_w, bar_h, SSD1306_WHITE);
    
    uint8_t current_render_percent = is_airmouse_locked ? global_wheel_sens_percent : global_sens_percent;
    int fill_w = (int)((float)current_render_percent / 100.0f * (bar_w - 4));
    display.fillRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, SSD1306_WHITE);
    
    display.display();
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
        display.print("M2: TOUCH & KNOB");
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