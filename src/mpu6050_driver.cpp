#include "config.h" // 🎯 必须加在最顶部以获取硬件引脚映射

// 如果你的底层读取函数需要兼容纯 C 项目，且原本头文件写了 extern "C"，这里也需要保持对齐
#include "mpu6050_driver.h" // 🎯 必须加在最顶部以获取函数声明
#include <Wire.h>   // Arduino 框架自带的 I2C 库

// ==========================================
// MPU6050 内部寄存器地址及兼容宏定义定义
// ==========================================
#define MPU6050_ADDR          0x68  // I2C 地址
#define MPU6050_SMPLRT_DIV    0x19  // 采样率分频器寄存器
#define MPU6050_CONFIG        0x1A  // 配置寄存器 (DLPF)
#define MPU6050_GYRO_CONFIG   0x1B  // 陀螺仪量程配置寄存器
#define MPU6050_ACCEL_CONFIG  0x1C  // 加速度计量程配置寄存器
#define MPU6050_ACCEL_XOUT_H  0x3B  // 加速度计 X 轴高字节寄存器
#define MPU6050_GYRO_XOUT_H   0x43  // 陀螺仪 X 轴高字节寄存器
#define MPU6050_PWR_MGMT_1    0x6B  // 电源管理寄存器 1
#define MPU6050_WHO_AM_I      0x75  // WHO_AM_I 寄存器地址 (用于检查设备 ID)

// 智能对齐新老项目的引脚宏命名
#ifndef I2C_SDA_PIN  // 如果没有定义 I2C_SDA_PIN
#define I2C_SDA_PIN     PIN_I2C_SDA // 使用 config.h 中的定义
#endif
#ifndef I2C_SCL_PIN // 如果没有定义 I2C_SCL_PIN
#define I2C_SCL_PIN     PIN_I2C_SCL // 使用 config.h 中的定义
#endif

// ==========================================
// 内部辅助静态函数
// ==========================================
// 向指定寄存器写入1个字节
static void write_register(uint8_t reg, uint8_t data) {  //🎯 静态函数，仅限本文件内使用
    Wire.beginTransmission(MPU6050_ADDR); // 开始 I2C 传输
    Wire.write(reg); // 写入寄存器地址
    Wire.write(data); // 写入数据
    Wire.endTransmission(); // 结束 I2C 传输
}

// ==========================================
// 外部导出函数实现
// ==========================================

bool mpu6050_init(void) { //🎯 必须加在最顶部以获取函数声明
    // 1. 初始化 I2C 引脚 (使用 config.h 中的定义)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // 初始化 I2C 总线，指定 SDA 和 SCL 引脚
    Wire.setClock(400000); // 提升 I2C 速率到 400kHz，减少读取延迟

    // 2. 检查设备是否在线 (读取 WHO_AM_I 寄存器)
    Wire.beginTransmission(MPU6050_ADDR); // 开始 I2C 传输
    Wire.write(MPU6050_WHO_AM_I); // 写入 WHO_AM_I 寄存器地址
    Wire.endTransmission(false); // 发送 Restart 信号
    Wire.requestFrom((uint16_t)MPU6050_ADDR, (uint8_t)1); // 请求读取 1 个字节 (WHO_AM_I 寄存器)
    
    if (Wire.available()) { // 如果有数据可读 
        uint8_t who_am_i = Wire.read(); // 读取 WHO_AM_I 寄存器的值
        if (who_am_i != 0x68) {  // 检查设备 ID 是否正确 (MPU6050 的默认 ID 是 0x68)
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
    if (data == nullptr) return; // 空指针检查，避免崩溃

    // 定位到第一个数据寄存器 (ACCEL_XOUT_H) 
    Wire.beginTransmission(MPU6050_ADDR); // 开始 I2C 传输
    Wire.write(MPU6050_ACCEL_XOUT_H); // 写入寄存器地址
    Wire.endTransmission(false); // 不释放总线

    // 连续请求读取 14 个字节 (7个寄存器，每个16位)
    Wire.requestFrom((uint16_t)MPU6050_ADDR, (uint8_t)14); // 请求读取 14 个字节 (加速度 X/Y/Z, 温度, 陀螺仪 X/Y/Z)

    if (Wire.available() == 14) {
        // I2C 读出的是大端模式 (高位在前，低位在后)，移位拼接成 16 位有符号整型
        data->accel_x = (int16_t)((Wire.read() << 8) | Wire.read()); // 读取加速度 X 轴
        data->accel_y = (int16_t)((Wire.read() << 8) | Wire.read()); // 读取加速度 Y 轴
        data->accel_z = (int16_t)((Wire.read() << 8) | Wire.read()); // 读取加速度 Z 轴
        
        data->temp    = (int16_t)((Wire.read() << 8) | Wire.read()); // 跳过/读取温度以对齐数据
        
        data->gyro_x  = (int16_t)((Wire.read() << 8) | Wire.read());    // 读取陀螺仪 X 轴
        data->gyro_y  = (int16_t)((Wire.read() << 8) | Wire.read()); // 读取陀螺仪 Y 轴
        data->gyro_z  = (int16_t)((Wire.read() << 8) | Wire.read()); // 读取陀螺仪 Z 轴
    }
}