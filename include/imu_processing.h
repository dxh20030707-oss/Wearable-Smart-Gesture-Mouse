#ifndef IMU_PROCESSING_H
#define IMU_PROCESSING_H

#include "mouse_mode.h" 

typedef struct {
    float pitch;
    float roll;
    float yaw;
} euler_angles_t;

#ifdef __cplusplus
extern "C" {
#endif

void imu_calibrate(void);
euler_angles_t imu_update_angles(void *raw_data, float dt);
void imu_reset_orientation(void);

// 🎯 新增核心导出接口：允许外界直接打包获取当前姿态角
euler_angles_t imu_get_current_angles(void);

#ifdef __cplusplus
}
#endif

#endif // IMU_PROCESSING_H