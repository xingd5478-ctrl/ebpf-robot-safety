#include "usbd_cdc_if.h"
#include <string.h>

#define APP_TX_BUF_SIZE   512U

static uint8_t UserTxBuffer[APP_TX_BUF_SIZE];

extern USBD_HandleTypeDef hUsbDeviceFS;

/* ========== CDC interface callbacks ========== */
static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len);

USBD_CDC_ItfTypeDef USBD_CDC_fops = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS
};

/* ========== Public API ========== */
void CDC_InitApp(void)
{
    /* Nothing to initialize */
}

int8_t CDC_SendData(uint8_t *buf, uint16_t len)
{
    if (!CDC_TxReady()) {
        return -1;  /* USB 繁忙, 丢弃本帧 */
    }
    if (len > APP_TX_BUF_SIZE) len = APP_TX_BUF_SIZE;
    memcpy(UserTxBuffer, buf, len);

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBuffer, len);
    return (USBD_CDC_TransmitPacket(&hUsbDeviceFS) == USBD_OK) ? 0 : -1;
}

uint8_t CDC_TxReady(void)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hcdc == NULL) return 0;
    return (hcdc->TxState == 0) ? 1 : 0;
}

/* ========== Callbacks ========== */
static int8_t CDC_Init_FS(void)
{
    return 0;
}

static int8_t CDC_DeInit_FS(void)
{
    return 0;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    switch (cmd) {
    case CDC_SET_LINE_CODING:
    case CDC_GET_LINE_CODING:
    case CDC_SET_CONTROL_LINE_STATE:
    default:
        break;
    }
    return 0;
}

static int8_t CDC_Receive_FS(uint8_t *Buf, uint32_t *Len)
{
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return 0;
}
