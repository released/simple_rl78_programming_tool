#ifndef NULINK2ME_IAP_TARGETDEV_H
#define NULINK2ME_IAP_TARGETDEV_H

#include "NuMicro.h"
#include "isp_user.h"

#define CONFIG_SIZE 16

void GetDataFlashInfo(uint32_t *address, uint32_t *size);
uint32_t GetApromSize(void);

#endif
