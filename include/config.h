#ifndef CONFIG_H
#define CONFIG_H

// 兼容 C/C++ 语言环境
#ifdef __cplusplus
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdbool.h>
#endif

// ==================== 引脚分配定义 (对应 GPIO 复用架构) ====================
#define PIN_I2C_SDA     8   // I2C 公共总线 SDA
#define PIN_I2C_SCL     9   // I2C 公共总线 SCL
#define PIN_MODE_SW     10  // 模式切换核心按键 (GPIO 10)

// 模式 1 (飞鼠模式) 引脚映射
#define PIN_M1_LEFT     0   // 鼠标左键 / 编码器 A相
#define PIN_M1_RIGHT    1   // 鼠标右键 / 编码器 B相
#define PIN_M1_PAUSE    2   // 飞鼠断开/恢复控制按键
#define PIN_M1_SENS     3   // 飞鼠灵敏度环形调节键

// 模式 2 (电容触控屏模式) 引脚映射
#define PIN_M2_TIM_CH1  0   // 编码器 A相
#define PIN_M2_TIM_CH2  1   // 编码器 B相
#define PIN_M2_INT      2   // 触控芯片中断 (INT)
#define PIN_M2_RST      3   // 触控芯片复位 (RST)

// ==================== 模式 3 专属 128-bit UUID ====================
#define CONFIG_SERVICE_UUID "12345678-1234-5678-1234-56789abcdef0"
#define CONFIG_CHAR_UUID    "12345678-1234-5678-1234-56789abcdef1"

// ==================== C++ 专属结构体与全局声明 ====================
#ifdef __cplusplus

// 系统三模式枚举
enum SystemMode {
    MODE_FLY_MOUSE = 0,  // 模式 1：飞鼠模式
    MODE_TOUCH_PAD = 1,  // 模式 2：电容触控屏模式
    MODE_APP_CONFIG = 2  // 模式 3：手机 App BLE 专属调参模式
};

// 全局统一参数结构体
struct SystemConfig {
    uint8_t air_dpi_level;     // 飞鼠灵敏度档位 (1 ~ 10)
    uint8_t touch_dpi_level;   // 触控/滚轮灵敏度档位 (1 ~ 10)
    uint8_t deadzone_px;       // 死区 (0 ~ 10 px)
    
    float air_gain;            // 映射后的飞鼠算法增益
    float touch_gain;          // 映射后的触控算法增益
};

extern SystemConfig g_cfg;
extern SystemMode g_current_mode;

void updateConfigGains();
void saveConfigToNVS();
void loadConfigFromNVS();

#endif // __cplusplus

#endif // CONFIG_H