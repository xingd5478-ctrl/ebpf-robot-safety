#include "system_config.h"
#include "data_protocol.h"  /* for crc16 */
#include <string.h>

/*
 * 存储地址: 使用倒数第二页 (留最后一页给可能的 bootloader 标志区)
 * STM32F103C8: 64KB Flash, 1KB/page, 最后页 0x0800FC00
 * 我们用 0x0800F800 (倒数第 2 页)
 *
 * 注意: flash_config.c 目前使用 0x0800FC00 (最后 1KB)
 * 新代码统一用 system_config, flash_config 保留供迁移使用。
 */
#define SYSCFG_FLASH_ADDR   ((uint32_t)0x0800F800)

/* ========== 计算 CRC (零化 crc 字段后对整个结构体求 CRC16) ========== */
static uint16_t calc_crc(const SystemConfig_t *cfg)
{
    uint8_t buf[sizeof(SystemConfig_t)];
    memcpy(buf, cfg, sizeof(SystemConfig_t));
    /* crc 字段位于结构体偏移 8 字节处 (magic:4 + version:4) */
    uint32_t crc_offset = (uint32_t)&((SystemConfig_t*)0)->crc;
    buf[crc_offset]     = 0;
    buf[crc_offset + 1] = 0;
    return crc16(buf, (uint16_t)sizeof(SystemConfig_t));
}

/* ========== 填充默认值 ========== */
void SysCfg_SetDefaults(SystemConfig_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic   = SYSCFG_MAGIC;
    cfg->version = SYSCFG_VERSION;
    cfg->dlpf    = SYSCFG_DEFAULT_DLPF;
    cfg->accel_fs = SYSCFG_DEFAULT_ACCEL_FS;
    cfg->gyro_fs  = SYSCFG_DEFAULT_GYRO_FS;
    cfg->rate_hz  = SYSCFG_DEFAULT_RATE_HZ;
    cfg->crc = calc_crc(cfg);
}

/* ========== 校验 CRC ========== */
int SysCfg_Validate(const SystemConfig_t *cfg)
{
    if (cfg->magic != SYSCFG_MAGIC)
        return 0;
    if (cfg->version == 0 || cfg->version > 10)
        return 0;
    uint16_t saved_crc = cfg->crc;
    /* 临时清除 crc 字段再算 */
    SystemConfig_t tmp;
    memcpy(&tmp, cfg, sizeof(tmp));
    tmp.crc = 0;
    uint16_t calculated = crc16((uint8_t*)&tmp, sizeof(tmp));
    return (saved_crc == calculated) ? 1 : 0;
}

/* ========== 加载 ========== */
void SysCfg_Load(SystemConfig_t *cfg)
{
    const SystemConfig_t *flash_cfg = (const SystemConfig_t*)SYSCFG_FLASH_ADDR;

    if (SysCfg_Validate(flash_cfg)) {
        memcpy(cfg, flash_cfg, sizeof(*cfg));
    } else {
        /* 无效配置: 用默认值 */
        SysCfg_SetDefaults(cfg);
    }
}

/* ========== 保存 ========== */
HAL_StatusTypeDef SysCfg_Save(const SystemConfig_t *cfg)
{
    SystemConfig_t save;
    memcpy(&save, cfg, sizeof(save));

    /* 重新计算 CRC */
    save.crc = 0;   /* 临时清零 */
    save.crc = calc_crc(&save);

    HAL_FLASH_Unlock();

    /* 擦除页 */
    FLASH_EraseInitTypeDef eraseInit = {
        .TypeErase   = FLASH_TYPEERASE_PAGES,
        .PageAddress = SYSCFG_FLASH_ADDR,
        .NbPages     = 1
    };
    uint32_t pageError;
    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK) {
        HAL_FLASH_Lock();
        return HAL_ERROR;
    }

    /* 写入 */
    const uint32_t *src = (const uint32_t*)&save;
    for (uint32_t i = 0; i < sizeof(save) / 4; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              SYSCFG_FLASH_ADDR + i * 4, src[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }

    HAL_FLASH_Lock();
    return HAL_OK;
}
