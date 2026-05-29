#ifndef __DATA_PROTOCOL_H
#define __DATA_PROTOCOL_H

#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 帧类型枚举 (统一管理) ========== */
typedef enum {
    FRAME_NONE          = 0x0000,

    /* 单节点帧类型 */
    FRAME_QUAT_ACCEL    = 0xAA55,   /* 四元数 + 加速度 (30B) */
    FRAME_EULER_ACCEL   = 0xAACE,   /* 欧拉角 + 加速度 (26B) */
    FRAME_RAW_6AXIS     = 0xAADD,   /* 原始6轴数据   (20B) */

    /* 多节点扩展 */
    FRAME_QUAT_NODE     = 0xBB66,   /* 多节点四元数帧 (32B) */

    /* 同步帧 */
    FRAME_SYNC          = 0xCC77,   /* 同步脉冲 (6B) */

    /* 协议控制 */
    FRAME_ACK           = 0xA500,   /* ACK 确认帧 (由 host 发回) */
} FrameType_t;

/* ========== ACK 协议常量 ========== */
#define PROTOCOL_ACK_BYTE    0xA5       /* Host 回传的单字节 ACK */
#define PROTOCOL_ACK_TIMEOUT 5  /* ACK 等待窗口 (ms) — 放宽以容忍 Windows USB 抖动 */
#define PROTOCOL_RETRY_MAX   3          /* 最大连续丢帧告警阈值 */

/* ========== 序列号 ========== */
#define PROTOCOL_SEQ_MASK    0x7FFF     /* 15位序列号, 最高位保留 */

/* ---------- 最大节点数 ---------- */
#define MAX_NODES        8

/* ---------- 节点数据结构 ---------- */
typedef struct {
    uint8_t  node_id;           /* 节点 ID (1~MAX_NODES) */
    float    q[4];              /* 四元数 */
    int16_t  accel[3];          /* 加速度原始值 */
    uint16_t timestamp;         /* 时间戳 (ms) */
} NodeData_t;

/* ---------- 校验 ---------- */
uint16_t crc16(const uint8_t *data, uint16_t len);

/* ---------- 协议初始化 (重置序列号) ---------- */
void Protocol_Init(void);

/* ---------- 帧构建 (不发送, 用于 USB CDC 等非 UART 通道) ---------- */
uint16_t Protocol_BuildQuatAndAccel(uint8_t *buf, uint16_t buf_size,
                                     const float q[4], const int16_t accel[3]);
uint16_t Protocol_BuildRaw6Axis(uint8_t *buf, uint16_t buf_size,
                                 const int16_t accel[3], const int16_t gyro[3]);

/* ---------- 单节点发送 ---------- */
HAL_StatusTypeDef Protocol_SendQuatAndAccel(UART_HandleTypeDef *huart,
                                             float *q, int16_t *accel);
HAL_StatusTypeDef Protocol_SendQuatAndAccel_DMA(UART_HandleTypeDef *huart,
                                                 float *q, int16_t *accel,
                                                 uint8_t *tx_buffer);
HAL_StatusTypeDef Protocol_SendEulerAndAccel(UART_HandleTypeDef *huart,
                                              float *euler, int16_t *accel);
HAL_StatusTypeDef Protocol_SendRaw(UART_HandleTypeDef *huart,
                                    int16_t *accel, int16_t *gyro);
HAL_StatusTypeDef Protocol_SendRaw_DMA(UART_HandleTypeDef *huart,
                                        int16_t *accel, int16_t *gyro,
                                        uint8_t *tx_buffer);

/* ---------- 多节点发送 ---------- */
HAL_StatusTypeDef Protocol_SendNodeData(UART_HandleTypeDef *huart,
                                         const NodeData_t *node);
HAL_StatusTypeDef Protocol_SendSync(UART_HandleTypeDef *huart);

/* ---------- ACK 检测 (Comm 任务在 DMA 发送完成后调用) ----------
 * 返回值: 1 = 收到 ACK, 0 = 超时/无 ACK
 * status: 传入系统状态指针以更新统计 (可为 NULL)
 * 注: 非阻塞式短超时轮询, 不影响任务调度
 */
int Protocol_CheckAck(UART_HandleTypeDef *huart);

/* ---------- 链路质量统计 ---------- */
typedef struct {
    uint32_t frames_sent;       /* 总发送帧数 */
    uint32_t acks_received;     /* 收到 ACK 数 */
    uint32_t acks_missed;       /* 未收到 ACK 数 */
    uint32_t consecutive_miss;  /* 连续未收到 ACK */
    uint8_t  link_status;       /* 0=未知, 1=正常, 2=告警, 3=断链 */
} LinkStats_t;

extern LinkStats_t g_link_stats;

#ifdef __cplusplus
}
#endif

#endif /* __DATA_PROTOCOL_H */
