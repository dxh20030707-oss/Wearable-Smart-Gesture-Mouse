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

euler_angles_t imu_update_angles(void *raw_data, float dt) {
    euler_angles_t angles; 
    
    angles.roll = current_roll;
    angles.pitch = current_pitch;
    angles.yaw = current_yaw;
    return angles;
}

void imu_reset_orientation(void) {
    current_yaw = 0.0f; 
}

euler_angles_t imu_get_current_angles(void) {
    euler_angles_t dummy_angles;
    dummy_angles.pitch = current_pitch;
    dummy_angles.roll = current_roll;
    dummy_angles.yaw = current_yaw;
    return dummy_angles;
}