#include "flash_config.h"
#include "main.h"

#if defined(STM32F103xE) || defined(STM32F103xG)
  #define FLASH_SAVE_ADDR  0x0807F800
#elif defined(STM32F103xC) || defined(STM32F103xD)
  #define FLASH_SAVE_ADDR  0x0803F800
#else
  #define FLASH_SAVE_ADDR  0x0800FC00
#endif

#define MAGIC_NUMBER  0x12345678

void Flash_LoadCalib(CalibData_t *calib)
{
    uint32_t *p = (uint32_t*)FLASH_SAVE_ADDR;
    if (p[0] == MAGIC_NUMBER) {
        *calib = *(CalibData_t*)p;
    } else {
        calib->accel_offset[0] = 0;
        calib->accel_offset[1] = 0;
        calib->accel_offset[2] = 0;
        calib->gyro_offset[0] = 0.0f;
        calib->gyro_offset[1] = 0.0f;
        calib->gyro_offset[2] = 0.0f;
        calib->magic = 0;
    }
}

void Flash_SaveCalib(CalibData_t *calib)
{
    HAL_FLASH_Unlock();

    // 擦除页（HAL 方式）
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = FLASH_SAVE_ADDR;
    eraseInit.NbPages = 1;
    HAL_FLASHEx_Erase(&eraseInit, &pageError);

    // 写入数据
    uint32_t *src = (uint32_t*)calib;
    for (int i = 0; i < sizeof(CalibData_t)/4; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_SAVE_ADDR + i*4, src[i]);
    }

    HAL_FLASH_Lock();
}