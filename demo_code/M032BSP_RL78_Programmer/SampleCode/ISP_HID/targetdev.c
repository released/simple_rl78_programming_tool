/***************************************************************************//**
 * @file     targetdev.c
 * @brief    ISP support function source file
 * @version  0x32
 * @date     14, June, 2017
 *
 * @note
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2017-2018 Nuvoton Technology Corp. All rights reserved.
 ******************************************************************************/
#include "targetdev.h"
#include "isp_user.h"

uint32_t GetApromSize(void)
{
    return APROM_APPLICATION_SIZE;
}

void GetDataFlashInfo(uint32_t *addr, uint32_t *size)
{
    /* No APROM-backed data-flash range is exposed by this IAP profile. */
    *addr = APROM_APPLICATION_END;
    *size = 0UL;
}

