#include "data_protocol.h"
#include <string.h>

/* ========== 全局链路统计 ========== */
LinkStats_t g_link_stats = {0};

/* ========== 序列号发生器 ========== */
static uint16_t s_seq_num = 0;

void Protocol_Init(void)
{
    s_seq_num = 0;
    memset(&g_link_stats, 0, sizeof(g_link_stats));
}

/* ========== CRC16-CCITT ========== */
uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ========== 写入帧头 (含序列号) ========== */
static void write_frame_header(uint8_t *buf, FrameType_t type,
                                uint8_t payload_len, uint8_t extra)
{
    buf[0] = (uint8_t)((uint16_t)type >> 8);
    buf[1] = (uint8_t)((uint16_t)type & 0xFF);
    buf[2] = payload_len;
    buf[3] = (uint8_t)(s_seq_num & 0xFF);       /* seq low */
    buf[4] = (uint8_t)((s_seq_num >> 8) & 0x7F); /* seq high (15-bit) */
    buf[5] = extra;                               /* 预留 (节点ID等) */
}

/* ========== 前置帧尾 CRC ========== */
static uint16_t finalize_frame(uint8_t *buf, uint16_t header_total_len,
                                uint16_t frame_len)
{
    uint16_t crc = crc16(buf, header_total_len);
    buf[frame_len - 2] = (uint8_t)(crc & 0xFF);
    buf[frame_len - 1] = (uint8_t)((crc >> 8) & 0xFF);
    /* 递增序列号 */
    s_seq_num = (s_seq_num + 1) & PROTOCOL_SEQ_MASK;
    return crc;
}

/* ========== ACK 检测 (只返回结果, 统计由调用方负责) ========== */
int Protocol_CheckAck(UART_HandleTypeDef *huart)
{
    uint8_t ack_byte = 0;
    if (HAL_UART_Receive(huart, &ack_byte, 1, PROTOCOL_ACK_TIMEOUT) == HAL_OK) {
        return (ack_byte == PROTOCOL_ACK_BYTE) ? 1 : 0;
    }
    return 0;
}

/* ========== 发送四元数 + 加速度 DMA 版 (30字节, 含序列号) ========== */
HAL_StatusTypeDef Protocol_SendQuatAndAccel_DMA(UART_HandleTypeDef *huart,
                                                 float *q, int16_t *accel,
                                                 uint8_t *tx_buffer)
{
    if (!huart || !q || !accel || !tx_buffer) return HAL_ERROR;

    /* 帧格式: [帧头6B][q:16B][accel:6B][CRC16:2B] = 30B */
    write_frame_header(tx_buffer, FRAME_QUAT_ACCEL, 24, 0);

    memcpy(&tx_buffer[6],  q, 16);        /* q0~q3 at offset 6  */
    memcpy(&tx_buffer[22], accel, 6);      /* ax,ay,az at offset 22 */

    finalize_frame(tx_buffer, 28, 30);     /* CRC over bytes 0-27, total 30 */

    return HAL_UART_Transmit_DMA(huart, tx_buffer, 30);
}

/* ========== 构建四元数 + 加速度帧 (30字节, 不发送) ========== */
uint16_t Protocol_BuildQuatAndAccel(uint8_t *buf, uint16_t buf_size,
                                     const float q[4], const int16_t accel[3])
{
    if (!buf || buf_size < 30) return 0;

    write_frame_header(buf, FRAME_QUAT_ACCEL, 24, 0);
    memcpy(&buf[6],  q, 16);
    memcpy(&buf[22], accel, 6);
    finalize_frame(buf, 28, 30);
    return 30;
}

/* ========== 构建原始6轴帧 (20字节, 不发送) ========== */
uint16_t Protocol_BuildRaw6Axis(uint8_t *buf, uint16_t buf_size,
                                 const int16_t accel[3], const int16_t gyro[3])
{
    if (!buf || buf_size < 20) return 0;

    write_frame_header(buf, FRAME_RAW_6AXIS, 18, 0);
    memcpy(&buf[6], accel, 6);
    memcpy(&buf[12], gyro, 6);
    finalize_frame(buf, 18, 20);
    return 20;
}

/* ========== 发送四元数 + 加速度 阻塞版 (30字节) ========== */
HAL_StatusTypeDef Protocol_SendQuatAndAccel(UART_HandleTypeDef *huart,
                                             float *q, int16_t *accel)
{
    uint8_t tx_buffer[30];
    if (!huart || !q || !accel) return HAL_ERROR;

    write_frame_header(tx_buffer, FRAME_QUAT_ACCEL, 24, 0);
    memcpy(&tx_buffer[6],  q, 16);
    memcpy(&tx_buffer[22], accel, 6);
    finalize_frame(tx_buffer, 28, 30);

    return HAL_UART_Transmit(huart, tx_buffer, 30, 100);
}

/* ========== 发送欧拉角 + 加速度 (26字节) ========== */
HAL_StatusTypeDef Protocol_SendEulerAndAccel(UART_HandleTypeDef *huart,
                                              float *euler, int16_t *accel)
{
    uint8_t tx_buffer[26];
    if (!huart || !euler || !accel) return HAL_ERROR;

    write_frame_header(tx_buffer, FRAME_EULER_ACCEL, 20, 0);
    memcpy(&tx_buffer[6], euler, 12);     /* roll, pitch, yaw */
    memcpy(&tx_buffer[18], accel, 6);      /* ax, ay, az */

    finalize_frame(tx_buffer, 24, 26);
    return HAL_UART_Transmit(huart, tx_buffer, 26, 100);
}

/* ========== 发送原始6轴数据 (20字节) ========== */
HAL_StatusTypeDef Protocol_SendRaw(UART_HandleTypeDef *huart,
                                    int16_t *accel, int16_t *gyro)
{
    uint8_t tx_buffer[20];
    if (!huart || !accel || !gyro) return HAL_ERROR;

    write_frame_header(tx_buffer, FRAME_RAW_6AXIS, 18, 0);
    memcpy(&tx_buffer[6], accel, 6);
    memcpy(&tx_buffer[12], gyro, 6);

    finalize_frame(tx_buffer, 18, 20);
    return HAL_UART_Transmit(huart, tx_buffer, 20, 100);
}

/* ========== 发送原始6轴数据 DMA 版 (20字节) ========== */
HAL_StatusTypeDef Protocol_SendRaw_DMA(UART_HandleTypeDef *huart,
                                        int16_t *accel, int16_t *gyro,
                                        uint8_t *tx_buffer)
{
    if (!huart || !accel || !gyro || !tx_buffer) return HAL_ERROR;

    write_frame_header(tx_buffer, FRAME_RAW_6AXIS, 18, 0);
    memcpy(&tx_buffer[6], accel, 6);
    memcpy(&tx_buffer[12], gyro, 6);

    finalize_frame(tx_buffer, 18, 20);
    return HAL_UART_Transmit_DMA(huart, tx_buffer, 20);
}

/* ========== 发送多节点数据帧 (32字节) ========== */
HAL_StatusTypeDef Protocol_SendNodeData(UART_HandleTypeDef *huart,
                                         const NodeData_t *node)
{
    uint8_t tx_buffer[32];
    if (!huart || !node) return HAL_ERROR;

    write_frame_header(tx_buffer, FRAME_QUAT_NODE, 26, node->node_id);
    memcpy(&tx_buffer[6], node->q, 16);
    memcpy(&tx_buffer[22], node->accel, 6);
    tx_buffer[28] = (uint8_t)(node->timestamp & 0xFF);
    tx_buffer[29] = (uint8_t)((node->timestamp >> 8) & 0xFF);

    finalize_frame(tx_buffer, 30, 32);
    return HAL_UART_Transmit(huart, tx_buffer, 32, 100);
}

/* ========== 发送同步帧 (6字节) ========== */
HAL_StatusTypeDef Protocol_SendSync(UART_HandleTypeDef *huart)
{
    uint8_t tx_buffer[6];
    if (!huart) return HAL_ERROR;

    write_frame_header(tx_buffer, FRAME_SYNC, 0, 0);
    finalize_frame(tx_buffer, 4, 6);
    return HAL_UART_Transmit(huart, tx_buffer, 6, 100);
}
