#ifndef __FLASH_CONFIG_H
#define __FLASH_CONFIG_H

#include <stdint.h>

typedef struct {
    int16_t accel_offset[3];
    float gyro_offset[3];
    uint32_t magic;
} CalibData_t;

void Flash_LoadCalib(CalibData_t *calib);
void Flash_SaveCalib(CalibData_t *calib);

#endif