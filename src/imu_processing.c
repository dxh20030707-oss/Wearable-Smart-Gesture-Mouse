#include "imu_processing.h"
#include "config.h"
#include "mpu6050_driver.h"
#include <math.h>

// 防御性定义 PI，规避与 Arduino.h 冲突
#ifndef PI
#define PI 3.1415926535f
#endif

#define GYRO_SCALE  65.5f  // 陀螺仪比例因子
#define ACCEL_SCALE 16384.0f  // 加速度计比例因子
#define ALPHA       0.98f  // 互补滤波系数

static float gyro_offset_x = 0.0f; // 陀螺仪偏移量
static float gyro_offset_y = 0.0f; // 陀螺仪偏移量
static float gyro_offset_z = 0.0f; // 陀螺仪偏移量

static float current_pitch = 0.0f; // 当前俯仰角
static float current_roll = 0.0f; // 当前翻滚角
static float current_yaw = 0.0f; // 当前偏航角

void imu_calibrate(void) { // 校准陀螺仪偏移量
    gyro_offset_x = 0.0f;  // 重置偏移量
    gyro_offset_y = 0.0f;  
    gyro_offset_z = 0.0f;
    current_pitch = 0.0f;
    current_roll = 0.0f;
    current_yaw = 0.0f;
}

euler_angles_t imu_update_angles(void *raw_data, float dt) { // 更新姿态角
    euler_angles_t angles;  // 定义姿态角结构体
    
    angles.roll = current_roll; // 使用当前翻滚角
    angles.pitch = current_pitch; // 使用当前俯仰角
    angles.yaw = current_yaw;  // 使用当前偏航角
    return angles; // 返回当前姿态角
}

void imu_reset_orientation(void) { // 重置姿态角
    current_yaw = 0.0f; 
}

euler_angles_t imu_get_current_angles(void) {  // 获取当前姿态角
    euler_angles_t dummy_angles; // 定义一个临时姿态角结构体
    dummy_angles.pitch = current_pitch; // 使用当前俯仰角
    dummy_angles.roll = current_roll;  //   使用当前翻滚角
    dummy_angles.yaw = current_yaw; // 使用当前偏航角
    return dummy_angles; // 返回当前姿态角
}