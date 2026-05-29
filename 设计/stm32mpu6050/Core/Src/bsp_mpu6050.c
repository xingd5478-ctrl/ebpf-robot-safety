#include "bsp_mpu6050.h"
#include <stdio.h>

/* ========== 灵敏度查表 ========== */
static const uint16_t accel_lsb_table[] = { 16384, 8192, 4096, 2048 };
static const float    gyro_lsb_table[]   = { 131.0f, 65.5f, 32.8f, 16.4f };

/* ========== 内部辅助函数 ========== */
static HAL_StatusTypeDef write_reg(MPU6050_Handle_t *dev,
                                    uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(dev->i2c, dev->address, reg,
                             I2C_MEMADD_SIZE_8BIT, &val, 1, 10);
}

static HAL_StatusTypeDef read_reg(MPU6050_Handle_t *dev,
                                   uint8_t reg, uint8_t *val)
{
    return HAL_I2C_Mem_Read(dev->i2c, dev->address, reg,
                            I2C_MEMADD_SIZE_8BIT, val, 1, 100);
}

/* ========== 更新内部灵敏度换算因子 ========== */
static void update_sensitivity(MPU6050_Handle_t *dev)
{
    dev->accel_lsb_per_g = (float)accel_lsb_table[dev->accel_fs];
    dev->gyro_lsb_per_dps = gyro_lsb_table[dev->gyro_fs];
}

/* ========== 初始化 ========== */
HAL_StatusTypeDef MPU6050_Init(MPU6050_Handle_t *dev)
{
    uint8_t whoami = 0;

    if (!dev || !dev->i2c) return HAL_ERROR;

    dev->address = MPU6050_ADDR;
    dev->accel_fs = MPU6050_ACCEL_FS_2G;
    dev->gyro_fs  = MPU6050_GYRO_FS_250DPS;
    dev->dlpf     = MPU6050_DLPF_94HZ;
    update_sensitivity(dev);

    /* ---- 1. WHO_AM_I 校验 ---- */
    if (read_reg(dev, MPU6050_WHO_AM_I, &whoami) != HAL_OK)
        return HAL_ERROR;

    if (whoami != MPU6050_WHO_AM_I_VALUE) {
        /* 传感器未响应或地址不匹配 */
        return HAL_ERROR;
    }

    /* ---- 2. 复位信号路径 ---- */
    write_reg(dev, MPU6050_SIGNAL_PATH_RESET, 0x07);
    HAL_Delay(10);

    /* ---- 3. 唤醒（PWR_MGMT_1 清零） ---- */
    if (write_reg(dev, MPU6050_PWR_MGMT_1, 0x00) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(10);

    /* ---- 4. 解除睡眠模式 ---- */
    if (write_reg(dev, MPU6050_PWR_MGMT_2, 0x00) != HAL_OK)
        return HAL_ERROR;

    /* ---- 5. 配置采样率分频 (DLPF 确定后由 SetSampleRate 设置) ---- */
    dev->sample_rate = 100;   /* 默认值, DLPF 配置后更新 */

    /* ---- 6. 配置DLPF ---- */
    MPU6050_SetDLPF(dev, dev->dlpf);
    MPU6050_SetSampleRate(dev, dev->sample_rate);    /* 根据 DLPF 计算 SMPLRT_DIV */

    /* ---- 7. 配置量程 ---- */
    MPU6050_SetAccelFullScale(dev, dev->accel_fs);
    MPU6050_SetGyroFullScale(dev, dev->gyro_fs);

    /* ---- 8. 配置时钟源为陀螺仪X轴PLL（精度最高） ---- */
    write_reg(dev, MPU6050_PWR_MGMT_1, 0x01);

    return HAL_OK;
}

/* ========== 配置加速度量程 ========== */
HAL_StatusTypeDef MPU6050_SetAccelFullScale(MPU6050_Handle_t *dev,
                                             MPU6050_AccelFullScale_t fs)
{
    if (!dev) return HAL_ERROR;
    dev->accel_fs = fs;
    update_sensitivity(dev);
    return write_reg(dev, MPU6050_ACCEL_CONFIG, (uint8_t)(fs << 3));
}

/* ========== 配置陀螺仪量程 ========== */
HAL_StatusTypeDef MPU6050_SetGyroFullScale(MPU6050_Handle_t *dev,
                                            MPU6050_GyroFullScale_t fs)
{
    if (!dev) return HAL_ERROR;
    dev->gyro_fs = fs;
    update_sensitivity(dev);
    return write_reg(dev, MPU6050_GYRO_CONFIG, (uint8_t)(fs << 3));
}

/* ========== 配置采样率 (SMPLRT_DIV) ========== */
HAL_StatusTypeDef MPU6050_SetSampleRate(MPU6050_Handle_t *dev, uint16_t rate_hz)
{
    uint8_t div;
    if (!dev || rate_hz == 0) return HAL_ERROR;

    /* 根据 DLPF 确定陀螺仪输出率:
     *   DLPF_CFG = 0 → 8kHz, DLPF_CFG = 1..6 → 1kHz */
    uint16_t gyro_rate = (dev->dlpf == MPU6050_DLPF_260HZ) ? 8000 : 1000;

    if (rate_hz > gyro_rate) rate_hz = gyro_rate;
    div = (uint8_t)(gyro_rate / rate_hz - 1);

    if (write_reg(dev, MPU6050_SMPLRT_DIV, div) != HAL_OK)
        return HAL_ERROR;

    dev->sample_rate = rate_hz;
    return HAL_OK;
}

/* ========== 配置DLPF ========== */
HAL_StatusTypeDef MPU6050_SetDLPF(MPU6050_Handle_t *dev,
                                   MPU6050_DLPF_Bandwidth_t bw)
{
    if (!dev) return HAL_ERROR;
    dev->dlpf = bw;
    /* DLPF_CFG 位于 CONFIG 寄存器的低3位 */
    return write_reg(dev, MPU6050_CONFIG, (uint8_t)(bw & 0x07));
}

/* ========== 读取加速度 ========== */
HAL_StatusTypeDef MPU6050_ReadAccel(MPU6050_Handle_t *dev,
                                     int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t buf[6];
    HAL_StatusTypeDef ret;

    if (!dev || !ax || !ay || !az) return HAL_ERROR;

    ret = HAL_I2C_Mem_Read(dev->i2c, dev->address, MPU6050_ACCEL_XOUT_H,
                           I2C_MEMADD_SIZE_8BIT, buf, 6, 100);
    if (ret != HAL_OK) return ret;

    *ax = (int16_t)((buf[0] << 8) | buf[1]);
    *ay = (int16_t)((buf[2] << 8) | buf[3]);
    *az = (int16_t)((buf[4] << 8) | buf[5]);
    return HAL_OK;
}

/* ========== 读取陀螺仪 ========== */
HAL_StatusTypeDef MPU6050_ReadGyro(MPU6050_Handle_t *dev,
                                    int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buf[6];
    HAL_StatusTypeDef ret;

    if (!dev || !gx || !gy || !gz) return HAL_ERROR;

    ret = HAL_I2C_Mem_Read(dev->i2c, dev->address, MPU6050_GYRO_XOUT_H,
                           I2C_MEMADD_SIZE_8BIT, buf, 6, 100);
    if (ret != HAL_OK) return ret;

    *gx = (int16_t)((buf[0] << 8) | buf[1]);
    *gy = (int16_t)((buf[2] << 8) | buf[3]);
    *gz = (int16_t)((buf[4] << 8) | buf[5]);
    return HAL_OK;
}

/* ========== 读取温度 ========== */
HAL_StatusTypeDef MPU6050_ReadTemp(MPU6050_Handle_t *dev, float *temp_c)
{
    uint8_t buf[2];
    int16_t raw;
    HAL_StatusTypeDef ret;

    if (!dev || !temp_c) return HAL_ERROR;

    ret = HAL_I2C_Mem_Read(dev->i2c, dev->address, MPU6050_TEMP_OUT_H,
                           I2C_MEMADD_SIZE_8BIT, buf, 2, 100);
    if (ret != HAL_OK) return ret;

    raw = (int16_t)((buf[0] << 8) | buf[1]);
    /* 温度公式: Temp = raw/340 + 36.53 (°C) */
    *temp_c = (float)raw / 340.0f + 36.53f;
    return HAL_OK;
}

/* ========== 一次性读取全部传感器数据 ========== */
HAL_StatusTypeDef MPU6050_ReadAll(MPU6050_Handle_t *dev,
                                   int16_t *ax, int16_t *ay, int16_t *az,
                                   int16_t *gx, int16_t *gy, int16_t *gz,
                                   float *temp_c)
{
    uint8_t buf[14];  // ACCEL(6) + TEMP(2) + GYRO(6)
    HAL_StatusTypeDef ret;

    if (!dev) return HAL_ERROR;

    /* 从 ACCEL_XOUT_H 开始突发读取14字节 */
    ret = HAL_I2C_Mem_Read(dev->i2c, dev->address, MPU6050_ACCEL_XOUT_H,
                           I2C_MEMADD_SIZE_8BIT, buf, 14, 10);
    if (ret != HAL_OK) return ret;

    if (ax) *ax = (int16_t)((buf[0] << 8) | buf[1]);
    if (ay) *ay = (int16_t)((buf[2] << 8) | buf[3]);
    if (az) *az = (int16_t)((buf[4] << 8) | buf[5]);

    if (temp_c) {
        int16_t raw_temp = (int16_t)((buf[6] << 8) | buf[7]);
        *temp_c = (float)raw_temp / 340.0f + 36.53f;
    }

    if (gx) *gx = (int16_t)((buf[8] << 8) | buf[9]);
    if (gy) *gy = (int16_t)((buf[10] << 8) | buf[11]);
    if (gz) *gz = (int16_t)((buf[12] << 8) | buf[13]);

    return HAL_OK;
}

/* ========== 睡眠/唤醒 ========== */
HAL_StatusTypeDef MPU6050_Sleep(MPU6050_Handle_t *dev)
{
    if (!dev) return HAL_ERROR;
    return write_reg(dev, MPU6050_PWR_MGMT_1, 0x40);  // SLEEP位置位
}

HAL_StatusTypeDef MPU6050_Wake(MPU6050_Handle_t *dev)
{
    if (!dev) return HAL_ERROR;
    return write_reg(dev, MPU6050_PWR_MGMT_1, 0x01);  // 恢复PLL时钟
}

/* ========== 自检 ========== */
HAL_StatusTypeDef MPU6050_SelfTest(MPU6050_Handle_t *dev)
{
    uint8_t accel_st = 0, gyro_st = 0;

    if (!dev) return HAL_ERROR;

    /* 启用自检（全量程） */
    write_reg(dev, MPU6050_ACCEL_CONFIG, 0xF0);  // accel selftest enable
    write_reg(dev, MPU6050_GYRO_CONFIG,  0xF0);  // gyro selftest enable
    HAL_Delay(20);

    /* 读取自检输出 - 如果非零即通过 */
    if (read_reg(dev, MPU6050_ACCEL_CONFIG, &accel_st) != HAL_OK)
        return HAL_ERROR;
    if (read_reg(dev, MPU6050_GYRO_CONFIG, &gyro_st) != HAL_OK)
        return HAL_ERROR;

    /* 恢复正常配置 */
    MPU6050_SetAccelFullScale(dev, dev->accel_fs);
    MPU6050_SetGyroFullScale(dev, dev->gyro_fs);

    return ((accel_st & 0xF0) && (gyro_st & 0xF0)) ? HAL_OK : HAL_ERROR;
}

/* ========== 调试：打印寄存器 ========== */
/* 注: 本函数使用 printf, 需要实现 _write 重定向到 UART 才能正常输出.
 * 在未重定向时调用将无输出. 仅为调试用途, 生产代码不调用. */
void MPU6050_DumpRegisters(MPU6050_Handle_t *dev)
{
    (void)dev;
}
