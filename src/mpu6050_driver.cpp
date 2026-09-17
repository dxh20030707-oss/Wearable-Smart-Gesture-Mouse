#include "config.h" // 🎯 必须加在最顶部以获取硬件引脚映射

// 如果你的底层读取函数需要兼容纯 C 项目，且原本头文件写了 extern "C"，这里也需要保持对齐
#include "mpu6050_driver.h"
#include <Wire.h>   // Arduino 框架自带的 I2C 库

// ==========================================
// MPU6050 内部寄存器地址及兼容宏定义定义
// ==========================================
#define MPU6050_ADDR          0x68 
#define MPU6050_SMPLRT_DIV    0x19
#define MPU6050_CONFIG        0x1A
#define MPU6050_GYRO_CONFIG   0x1B
#define MPU6050_ACCEL_CONFIG  0x1C
#define MPU6050_ACCEL_XOUT_H  0x3B
#define MPU6050_GYRO_XOUT_H   0x43
#define MPU6050_PWR_MGMT_1    0x6B
#define MPU6050_WHO_AM_I      0x75

// 智能对齐新老项目的引脚宏命名
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN     PIN_I2C_SDA
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN     PIN_I2C_SCL
#endif

// ==========================================
// 内部辅助静态函数
// ==========================================
// 向指定寄存器写入1个字节
static void write_register(uint8_t reg, uint8_t data) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(reg);
    Wire.write(data);
    Wire.endTransmission();
}

// ==========================================
// 外部导出函数实现
// ==========================================

bool mpu6050_init(void) {
    // 1. 初始化 I2C 引脚 (使用 config.h 中的定义)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000); // 提升 I2C 速率到 400kHz，减少读取延迟

    // 2. 检查设备是否在线 (读取 WHO_AM_I 寄存器)
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_WHO_AM_I);
    Wire.endTransmission(false); // 发送 Restart 信号
    Wire.requestFrom((uint16_t)MPU6050_ADDR, (uint8_t)1);
    
    if (Wire.available()) {
        uint8_t who_am_i = Wire.read();
        if (who_am_i != 0x68) {
            return false; // 通信成功但 ID 不对
        }
    } else {
        return false; // I2C 通信失败 (可能是接线断了)
    }

    // 3. 唤醒芯片 (解除休眠模式)
    write_register(MPU6050_PWR_MGMT_1, 0x00);

    // 4. 配置陀螺仪量程 ±500°/s
    write_register(MPU6050_GYRO_CONFIG, 0x08);

    // 5. 配置加速度计量程 ±2g
    write_register(MPU6050_ACCEL_CONFIG, 0x00);

    // 6. 配置数字低通滤波器 (DLPF) 约 44Hz 带宽，过滤颤抖硬件噪声
    write_register(MPU6050_CONFIG, 0x03);

    return true; // 初始化成功
}

void mpu6050_read_raw(mpu6050_raw_data_t *data) {
    if (data == nullptr) return;

    // 定位到第一个数据寄存器 (ACCEL_XOUT_H)
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_ACCEL_XOUT_H);
    Wire.endTransmission(false); // 不释放总线

    // 连续请求读取 14 个字节 (7个寄存器，每个16位)
    Wire.requestFrom((uint16_t)MPU6050_ADDR, (uint8_t)14);

    if (Wire.available() == 14) {
        // I2C 读出的是大端模式 (高位在前，低位在后)，移位拼接成 16 位有符号整型
        data->accel_x = (int16_t)((Wire.read() << 8) | Wire.read());
        data->accel_y = (int16_t)((Wire.read() << 8) | Wire.read());
        data->accel_z = (int16_t)((Wire.read() << 8) | Wire.read());
        
        data->temp    = (int16_t)((Wire.read() << 8) | Wire.read()); // 跳过/读取温度以对齐数据
        
        data->gyro_x  = (int16_t)((Wire.read() << 8) | Wire.read());
        data->gyro_y  = (int16_t)((Wire.read() << 8) | Wire.read());
        data->gyro_z  = (int16_t)((Wire.read() << 8) | Wire.read());
    }
}