#ifndef MPU6050_DRIVER_H
#define MPU6050_DRIVER_H

#include <stdint.h>

// 原始数据结构体定义
typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t temp;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} mpu6050_raw_data_t;

#ifdef __cplusplus
extern "C" {
#endif

bool mpu6050_init(void);
void mpu6050_read_raw(mpu6050_raw_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_DRIVER_H