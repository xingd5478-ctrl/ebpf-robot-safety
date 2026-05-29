#ifndef __USBD_DESC_H
#define __USBD_DESC_H

#include "usbd_def.h"

#define DEVICE_ID1          (0x1FFFF7E8)
#define DEVICE_ID2          (0x1FFFF7EC)
#define DEVICE_ID3          (0x1FFFF7F0)

#define USB_SIZ_STRING_SERIAL       0x1A

extern USBD_DescriptorsTypeDef VCP_Desc;

#endif /* __USBD_DESC_H */
