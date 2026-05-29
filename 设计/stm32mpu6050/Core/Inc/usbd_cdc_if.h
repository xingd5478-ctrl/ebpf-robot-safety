#ifndef __USBD_CDC_IF_H
#define __USBD_CDC_IF_H

#include "usbd_cdc.h"

/* CDC Interface callback structure */
extern USBD_CDC_ItfTypeDef USBD_CDC_fops;

/* CDC application initialization */
void CDC_InitApp(void);

/* Send data over CDC (called from app tasks) */
int8_t CDC_SendData(uint8_t *buf, uint16_t len);

/* Get CDC Tx ready status (0=busy, 1=ready) */
uint8_t CDC_TxReady(void);

#endif /* __USBD_CDC_IF_H */
