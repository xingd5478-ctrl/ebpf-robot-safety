#ifndef __SENSOR_FUSION_H
#define __SENSOR_FUSION_H

#include <stdint.h>

/* ========== 融合状态结构体 (可重入, 无全局变量) ========== */
typedef struct {
    /* 四元数 */
    float q0, q1, q2, q3;
    /* PI 控制器积分项 */
    float integralFBx, integralFBy, integralFBz;
} FusionState;

/* ========== API ========== */

/* 初始化融合状态 (清零四元数、积分项) */
void Fusion_Init(FusionState *state);

/* 重置融合状态 (同 Init) */
void Fusion_Reset(FusionState *state);

/*
 * 更新姿态融合 (Madgwick 算法)
 *   state:    融合状态指针
 *   ax..az:  加速度计原始值 (LSB)
 *   gx..gz:  陀螺仪原始值 (LSB)
 *   dt:      采样间隔 (秒)
 *   q_out:   输出四元数 [4] (可为 NULL)
 *   roll/pitch/yaw: 输出欧拉角 (可为 NULL, 单位: 度)
 */
void Fusion_Update(FusionState *state,
                   int16_t ax, int16_t ay, int16_t az,
                   int16_t gx, int16_t gy, int16_t gz,
                   float dt,
                   float *q_out,
                   float *roll, float *pitch, float *yaw);

/* 从当前状态读取欧拉角 (不触发更新) */
void Fusion_GetEuler(const FusionState *state,
                     float *roll, float *pitch, float *yaw);

#endif /* __SENSOR_FUSION_H */
