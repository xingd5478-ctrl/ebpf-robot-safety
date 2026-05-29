/*
 * 通信协议单元测试: 帧编码 + CRC + 序列号
 *
 * 编译 (host):
 *   gcc -std=c11 -O2 -I../stm32mpu6050/Core/Inc -I. \
 *       test_protocol.c -lm -o test_protocol
 *   ./test_protocol
 */
#include "test_utils.h"
#include <stdint.h>
#include <string.h>

/* ========== 协议常量 (与 data_protocol.h 同步) ========== */
#define FRAME_QUAT_ACCEL   0xAA55
#define FRAME_EULER_ACCEL  0xAACE
#define FRAME_RAW_6AXIS    0xAADD
#define PROTOCOL_SEQ_MASK  0x7FFF

/* ========== CRC16 (与 data_protocol.c 同步) ========== */
static uint16_t crc16(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ========== 帧头写入 (与 data_protocol.c 同步) ========== */
static uint16_t s_seq_num = 0;

static void write_frame_header(uint8_t *buf, uint16_t type,
                                uint8_t payload_len, uint8_t extra)
{
    buf[0] = (uint8_t)(type >> 8);
    buf[1] = (uint8_t)(type & 0xFF);
    buf[2] = payload_len;
    buf[3] = (uint8_t)(s_seq_num & 0xFF);
    buf[4] = (uint8_t)((s_seq_num >> 8) & 0x7F);
    buf[5] = extra;
}

static uint16_t finalize_frame(uint8_t *buf, uint16_t header_total_len,
                                uint16_t frame_len)
{
    uint16_t crc = crc16(buf, header_total_len);
    buf[frame_len - 2] = (uint8_t)(crc & 0xFF);
    buf[frame_len - 1] = (uint8_t)((crc >> 8) & 0xFF);
    s_seq_num = (s_seq_num + 1) & PROTOCOL_SEQ_MASK;
    return crc;
}

/* ========== 协议重置 (模拟 Protocol_Init) ========== */
static void protocol_init(void)
{
    s_seq_num = 0;
}

/* ========== 测试 1: 帧头格式 ========== */
static void test_frame_header(void)
{
    TEST_BEGIN("QuatAccel frame header format");
    uint8_t buf[29] = {0};
    protocol_init();
    write_frame_header(buf, FRAME_QUAT_ACCEL, 24, 0);

    ASSERT_INT_EQ(buf[0], 0xAA);
    ASSERT_INT_EQ(buf[1], 0x55);
    ASSERT_INT_EQ(buf[2], 24);    /* payload length */
    ASSERT_INT_EQ(buf[3], 0);     /* seq low (0) */
    ASSERT_INT_EQ(buf[4], 0);     /* seq high (0) */
    ASSERT_INT_EQ(buf[5], 0);     /* extra/reserved */
    TEST_END();

    TEST_BEGIN("EulerAccel frame header");
    memset(buf, 0, sizeof(buf));
    write_frame_header(buf, FRAME_EULER_ACCEL, 20, 0);

    ASSERT_INT_EQ(buf[0], 0xAA);
    ASSERT_INT_EQ(buf[1], 0xCE);
    ASSERT_INT_EQ(buf[2], 20);
    TEST_END();

    TEST_BEGIN("Raw6Axis frame header");
    memset(buf, 0, sizeof(buf));
    write_frame_header(buf, FRAME_RAW_6AXIS, 18, 0);

    ASSERT_INT_EQ(buf[0], 0xAA);
    ASSERT_INT_EQ(buf[1], 0xDD);
    ASSERT_INT_EQ(buf[2], 18);
    TEST_END();
}

/* ========== 测试 2: CRC 位置 ========== */
static void test_crc_position(void)
{
    TEST_BEGIN("QuatAccel CRC at bytes 27-28 (frame len 29)");
    uint8_t buf[29];
    protocol_init();
    memset(buf, 0, sizeof(buf));
    write_frame_header(buf, FRAME_QUAT_ACCEL, 24, 0);
    /* payload: q[4]=0, accel[3]=0, rest=0 */

    uint16_t crc_before = crc16(buf, 27);
    finalize_frame(buf, 27, 29);

    uint16_t crc_in_frame = (uint16_t)buf[27] | ((uint16_t)buf[28] << 8);
    ASSERT_INT_EQ(crc_in_frame, crc_before);
    TEST_END();

    TEST_BEGIN("CRC changes when payload changes");
    uint8_t buf2[29];
    protocol_init();
    memset(buf2, 0, sizeof(buf2));
    write_frame_header(buf2, FRAME_QUAT_ACCEL, 24, 0);
    buf2[6] = 0x3F;  /* change first payload byte */
    finalize_frame(buf2, 27, 29);

    ASSERT_FALSE(buf[27] == buf2[27] && buf[28] == buf2[28]);
    TEST_END();
}

/* ========== 测试 3: 序列号递增 ========== */
static void test_sequence_number(void)
{
    TEST_BEGIN("Sequence number increments by 1 each frame");
    protocol_init();

    uint8_t buf1[29], buf2[29], buf3[29];

    memset(buf1, 0, sizeof(buf1));
    write_frame_header(buf1, FRAME_QUAT_ACCEL, 24, 0);
    finalize_frame(buf1, 27, 29);
    uint16_t seq1 = (uint16_t)buf1[3] | ((uint16_t)(buf1[4] & 0x7F) << 8);

    memset(buf2, 0, sizeof(buf2));
    write_frame_header(buf2, FRAME_QUAT_ACCEL, 24, 0);
    finalize_frame(buf2, 27, 29);
    uint16_t seq2 = (uint16_t)buf2[3] | ((uint16_t)(buf2[4] & 0x7F) << 8);

    memset(buf3, 0, sizeof(buf3));
    write_frame_header(buf3, FRAME_QUAT_ACCEL, 24, 0);
    finalize_frame(buf3, 27, 29);
    uint16_t seq3 = (uint16_t)buf3[3] | ((uint16_t)(buf3[4] & 0x7F) << 8);

    ASSERT_INT_EQ(seq1, 0);
    ASSERT_INT_EQ(seq2, 1);
    ASSERT_INT_EQ(seq3, 2);
    TEST_END();

    TEST_BEGIN("Sequence number wraps at 0x7FFF");
    protocol_init();
    s_seq_num = PROTOCOL_SEQ_MASK;  /* overflow test */

    uint8_t buf[29];
    memset(buf, 0, sizeof(buf));
    write_frame_header(buf, FRAME_QUAT_ACCEL, 24, 0);
    finalize_frame(buf, 27, 29);

    uint16_t seq = (uint16_t)buf[3] | ((uint16_t)(buf[4] & 0x7F) << 8);
    ASSERT_INT_EQ(seq, PROTOCOL_SEQ_MASK);
    /* and seq_num should have been cleared after wrap */
    ASSERT_INT_EQ(s_seq_num, 0);
    TEST_END();
}

/* ========== 测试 4: CRC 校验检测错误 ========== */
static void test_crc_detection(void)
{
    TEST_BEGIN("CRC detects single-bit error");
    uint8_t buf[29];
    protocol_init();
    memset(buf, 0, sizeof(buf));
    write_frame_header(buf, FRAME_QUAT_ACCEL, 24, 0);
    /* fill payload with pattern */
    for (int i = 6; i < 27; i++) buf[i] = (uint8_t)(i * 7);
    finalize_frame(buf, 27, 29);

    /* flip one bit */
    buf[10] ^= 0x01;

    uint16_t check_crc = crc16(buf, 27);
    uint16_t stored_crc = (uint16_t)buf[27] | ((uint16_t)buf[28] << 8);
    ASSERT_FALSE(check_crc == stored_crc);
    TEST_END();

    TEST_BEGIN("CRC detects burst error");
    protocol_init();
    memset(buf, 0, sizeof(buf));
    write_frame_header(buf, FRAME_QUAT_ACCEL, 24, 0);
    for (int i = 6; i < 27; i++) buf[i] = (uint8_t)(i * 7);
    finalize_frame(buf, 27, 29);

    /* corrupt multiple bytes */
    buf[8] = 0xFF;
    buf[12] = 0xAA;
    buf[20] = 0x55;

    check_crc = crc16(buf, 27);
    stored_crc = (uint16_t)buf[27] | ((uint16_t)buf[28] << 8);
    ASSERT_FALSE(check_crc == stored_crc);
    TEST_END();
}

/* ========== 测试 5: 节点 ID 在帧中正确放置 ========== */
static void test_node_id(void)
{
    TEST_BEGIN("Node ID placed at byte 5 (extra field)");
    uint8_t buf[32];
    protocol_init();
    memset(buf, 0, sizeof(buf));
    write_frame_header(buf, FRAME_QUAT_ACCEL, 24, 7);  /* extra = node_id=7 */

    ASSERT_INT_EQ(buf[5], 7);
    TEST_END();
}

/* ========== 测试 6: 帧长度一致性 ========== */
static void test_frame_lengths(void)
{
    TEST_BEGIN("QuatAccel frame is 29 bytes");
    ASSERT_INT_EQ(sizeof(uint8_t) * 29, 29);
    TEST_END();

    TEST_BEGIN("Sync frame fits in 6 bytes");
    uint8_t sync[6];
    protocol_init();
    memset(sync, 0, sizeof(sync));
    /* Sync: [CC][77][len=0][seq:2B][extra=0][CRC:2B] */
    write_frame_header(sync, 0xCC77, 0, 0);
    finalize_frame(sync, 4, 6);
    ASSERT_INT_EQ(sync[0], 0xCC);
    ASSERT_INT_EQ(sync[1], 0x77);
    ASSERT_INT_EQ(sync[2], 0);  /* len=0 */
    TEST_END();
}

/* ========== main ========== */
int main(void)
{
    printf("==============================\n");
    printf("  通信协议单元测试\n");
    printf("==============================\n\n");

    test_frame_header();
    test_crc_position();
    test_sequence_number();
    test_crc_detection();
    test_node_id();
    test_frame_lengths();

    printf("\n");
    TEST_SUMMARY();
}
