#ifndef __BSP_MPU6050_H
#define __BSP_MPU6050_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== MPU6050 寄存器地址 ========== */
#define MPU6050_ADDR           0x68 << 1
#define MPU6050_WHO_AM_I       0x75
#define MPU6050_WHO_AM_I_VALUE 0x68

#define MPU6050_PWR_MGMT_1     0x6B
#define MPU6050_SMPLRT_DIV     0x19
#define MPU6050_CONFIG         0x1A
#define MPU6050_GYRO_CONFIG    0x1B
#define MPU6050_ACCEL_CONFIG   0x1C

#define MPU6050_ACCEL_XOUT_H   0x3B
#define MPU6050_GYRO_XOUT_H    0x43
#define MPU6050_TEMP_OUT_H     0x41

#define MPU6050_SIGNAL_PATH_RESET 0x68
#define MPU6050_USER_CTRL         0x6A
#define MPU6050_PWR_MGMT_2        0x6C

/* ========== 加速度计量程枚举 ========== */
typedef enum {
    MPU6050_ACCEL_FS_2G  = 0,   // ±2g,  LSB/g = 16384
    MPU6050_ACCEL_FS_4G  = 1,   // ±4g,  LSB/g = 8192
    MPU6050_ACCEL_FS_8G  = 2,   // ±8g,  LSB/g = 4096
    MPU6050_ACCEL_FS_16G = 3,   // ±16g, LSB/g = 2048
} MPU6050_AccelFullScale_t;

/* ========== 陀螺仪量程枚举 ========== */
typedef enum {
    MPU6050_GYRO_FS_250DPS  = 0,  // ±250 °/s,   LSB/°/s = 131
    MPU6050_GYRO_FS_500DPS  = 1,  // ±500 °/s,   LSB/°/s = 65.5
    MPU6050_GYRO_FS_1000DPS = 2,  // ±1000 °/s,  LSB/°/s = 32.8
    MPU6050_GYRO_FS_2000DPS = 3,  // ±2000 °/s,  LSB/°/s = 16.4
} MPU6050_GyroFullScale_t;

/* ========== DLPF带宽配置 ========== */
typedef enum {
    MPU6050_DLPF_260HZ = 0,   // accel: 260Hz, gyro: 256Hz
    MPU6050_DLPF_184HZ = 1,   // accel: 184Hz, gyro: 188Hz
    MPU6050_DLPF_94HZ  = 2,   // accel:  94Hz, gyro:  98Hz
    MPU6050_DLPF_44HZ  = 3,   // accel:  44Hz, gyro:  42Hz
    MPU6050_DLPF_21HZ  = 4,   // accel:  21Hz, gyro:  20Hz
    MPU6050_DLPF_10HZ  = 5,   // accel:  10Hz, gyro:  10Hz
    MPU6050_DLPF_5HZ   = 6,   // accel:   5Hz, gyro:   5Hz
} MPU6050_DLPF_Bandwidth_t;

/* ========== MPU6050 设备句柄 ========== */
typedef struct {
    I2C_HandleTypeDef  *i2c;             // I2C总线句柄指针
    uint8_t             address;          // I2C设备地址 (7-bit左对齐)
    MPU6050_AccelFullScale_t  accel_fs;  // 加速度计量程
    MPU6050_GyroFullScale_t   gyro_fs;   // 陀螺仪量程
    MPU6050_DLPF_Bandwidth_t  dlpf;      // 数字低通滤波器带宽
    uint16_t            sample_rate;      // 实际采样率 (Hz)

    /* 查表：当前配置下的灵敏度换算因子 */
    float accel_lsb_per_g;    // 加速度 LSB/g
    float gyro_lsb_per_dps;   // 陀螺仪 LSB/(°/s)
} MPU6050_Handle_t;

/* ========== API 函数声明 ========== */

// 初始化并配置MPU6050（包含WHO_AM_I校验）
HAL_StatusTypeDef MPU6050_Init(MPU6050_Handle_t *dev);

// 配置加速度计量程（默认±2g）
HAL_StatusTypeDef MPU6050_SetAccelFullScale(MPU6050_Handle_t *dev,
                                             MPU6050_AccelFullScale_t fs);

// 配置陀螺仪量程（默认±250°/s）
HAL_StatusTypeDef MPU6050_SetGyroFullScale(MPU6050_Handle_t *dev,
                                            MPU6050_GyroFullScale_t fs);

// 配置DLPF带宽（默认260Hz）
HAL_StatusTypeDef MPU6050_SetDLPF(MPU6050_Handle_t *dev,
                                   MPU6050_DLPF_Bandwidth_t bw);

// 配置采样率 (内部 SMPLRT_DIV, 根据 DLPF 自动计算分频系数)
HAL_StatusTypeDef MPU6050_SetSampleRate(MPU6050_Handle_t *dev, uint16_t rate_hz);

// 读取三轴加速度原始值
HAL_StatusTypeDef MPU6050_ReadAccel(MPU6050_Handle_t *dev,
                                     int16_t *ax, int16_t *ay, int16_t *az);

// 读取三轴陀螺仪原始值
HAL_StatusTypeDef MPU6050_ReadGyro(MPU6050_Handle_t *dev,
                                    int16_t *gx, int16_t *gy, int16_t *gz);

// 读取温度传感器 (°C)
HAL_StatusTypeDef MPU6050_ReadTemp(MPU6050_Handle_t *dev, float *temp_c);

// 读取所有传感器数据（一次性I2C突发读，效率最高）
HAL_StatusTypeDef MPU6050_ReadAll(MPU6050_Handle_t *dev,
                                   int16_t *ax, int16_t *ay, int16_t *az,
                                   int16_t *gx, int16_t *gy, int16_t *gz,
                                   float *temp_c);

// 进入/退出睡眠模式
HAL_StatusTypeDef MPU6050_Sleep(MPU6050_Handle_t *dev);
HAL_StatusTypeDef MPU6050_Wake(MPU6050_Handle_t *dev);

// 自检
HAL_StatusTypeDef MPU6050_SelfTest(MPU6050_Handle_t *dev);

// 便捷函数：打印当前寄存器状态（调试用）
void MPU6050_DumpRegisters(MPU6050_Handle_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_MPU6050_H */
