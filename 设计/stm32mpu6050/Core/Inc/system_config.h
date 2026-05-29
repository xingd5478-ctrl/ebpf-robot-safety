#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <stdint.h>
#include "main.h"

/* ========== 配置版本 & Magic ========== */
#define SYSCFG_MAGIC      0x53434746      /* "SCFG" */
#define SYSCFG_VERSION    3

/* ========== 配置结构 (存储于 Flash 末页) ========== */
typedef struct __attribute__((packed)) {
    /* 头部 */
    uint32_t    magic;                  /* 有效性标识 */
    uint32_t    version;                /* 结构版本号 */
    uint16_t    crc;                    /* CRC16-CCITT (从 version 到末尾) */

    /* 标定数据 */
    int16_t     accel_offset[3];        /* 加速度偏移 (ADC LSB) */
    float       gyro_offset[3];         /* 陀螺偏移 (dps) */

    /* 运行时参数 (与 ConfigParam_t 同步) */
    uint8_t     dlpf;                   /* DLPF 带宽 (0-6) */
    uint8_t     accel_fs;               /* 加速度量程 (0-3) */
    uint8_t     gyro_fs;                /* 陀螺量程 (0-3) */
    uint16_t    rate_hz;                /* 采集频率 (10-500 Hz) */

    /* 扩充预留 */
    uint8_t     reserved[16];
} SystemConfig_t;

/* ========== 默认值 ========== */
#define SYSCFG_DEFAULT_DLPF     2   /* MPU6050_DLPF_94HZ (模态分析/通用) */
#define SYSCFG_DEFAULT_ACCEL_FS 0
#define SYSCFG_DEFAULT_GYRO_FS  0
#define SYSCFG_DEFAULT_RATE_HZ  100

/* ========== API ========== */

/* 加载配置 (失败时填入默认值) */
void SysCfg_Load(SystemConfig_t *cfg);

/* 保存配置 (擦除+写入 Flash, 会计算 CRC) */
HAL_StatusTypeDef SysCfg_Save(const SystemConfig_t *cfg);

/* 填充默认值 */
void SysCfg_SetDefaults(SystemConfig_t *cfg);

/* 校验配置 CRC (返回 1=有效) */
int  SysCfg_Validate(const SystemConfig_t *cfg);

#endif /* SYSTEM_CONFIG_H */
