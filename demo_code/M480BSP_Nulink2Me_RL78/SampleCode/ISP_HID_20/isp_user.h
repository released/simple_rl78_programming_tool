#ifndef NULINK2ME_IAP_ISP_USER_H
#define NULINK2ME_IAP_ISP_USER_H

#include <string.h>

#include "iap_layout.h"
#include "fmc_user.h"

#define FW_VERSION 0x35

#define CMD_UPDATE_APROM 0x000000A0UL
#define CMD_UPDATE_CONFIG 0x000000A1UL
#define CMD_READ_CONFIG 0x000000A2UL
#define CMD_ERASE_ALL 0x000000A3UL
#define CMD_SYNC_PACKNO 0x000000A4UL
#define CMD_GET_FWVER 0x000000A6UL
#define CMD_RUN_APROM 0x000000ABUL
#define CMD_RUN_LDROM 0x000000ACUL
#define CMD_RESET 0x000000ADUL
#define CMD_CONNECT 0x000000AEUL
#define CMD_GET_DEVICEID 0x000000B1UL
#define CMD_UPDATE_DATAFLASH 0x000000C3UL
#define CMD_RESEND_PACKET 0x000000FFUL

int ParseCmd(unsigned char *buffer, uint8_t length);
extern uint32_t g_apromSize;
extern uint32_t g_dataFlashAddr;
extern uint32_t g_dataFlashSize;
extern __ALIGNED(4) uint8_t response_buff[64];
extern __ALIGNED(4) uint8_t usb_rcvbuf[64];

#endif
