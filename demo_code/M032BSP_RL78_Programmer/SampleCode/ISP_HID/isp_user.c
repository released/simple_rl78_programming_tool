/***************************************************************************//**
 * @file     isp_user.c
 * @brief    ISP Command source file
 * @version  0x34
 *
 * @note
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2017-2018 Nuvoton Technology Corp. All rights reserved.
 ******************************************************************************/
#include <stdio.h>
#include "isp_user.h"
#include "fmc_user.h"

#define ISP_CBS_APROM_WITH_IAP (2UL << FMC_ISPSTS_CBS_Pos)

#if 0
#define RSTSTS      RSTSRC
#define ISPCTL      ISPCON
#endif

volatile uint8_t bISPDataReady;
#ifdef __ICCARM__
#pragma data_alignment=4
uint8_t response_buff[64];
static uint8_t aprom_buf[FMC_FLASH_PAGE_SIZE];
#else
uint8_t response_buff[64] __attribute__((aligned(4)));
static uint8_t aprom_buf[FMC_FLASH_PAGE_SIZE] __attribute__((aligned(4)));
#endif
uint32_t bUpdateApromCmd;
uint32_t g_apromSize, g_dataFlashAddr, g_dataFlashSize;

__STATIC_INLINE uint16_t Checksum(unsigned char *buf, int len)
{
    int i;
    uint16_t c;

    for (c = 0, i = 0 ; i < len; i++)
    {
        c += buf[i];
    }

    return (c);
}

static int CalCheckSum(uint32_t start, uint32_t len, uint16_t *checksum)
{
    uint32_t offset;
    uint32_t chunk;
    uint16_t lcksum;
    int result;

    if (checksum == NULL)
    {
        return -1;
    }
    lcksum = 0;

    for (offset = 0UL; offset < len; offset += FMC_FLASH_PAGE_SIZE)
    {
        chunk = len - offset;

        if (chunk > FMC_FLASH_PAGE_SIZE)
        {
            chunk = FMC_FLASH_PAGE_SIZE;
        }

        result = ReadData(start + offset, start + offset + chunk,
                          (unsigned int *)aprom_buf);
        if (result != 0)
        {
            return result;
        }
        lcksum += Checksum(aprom_buf, (int)chunk);
    }

    *checksum = lcksum;
    return 0;
}

static uint8_t Isp_IsIapConfigurationValid(void)
{
    return ((FMC->ISPSTS & FMC_ISPSTS_CBS_Msk) ==
            ISP_CBS_APROM_WITH_IAP) ? TRUE : FALSE;
}

static int32_t Isp_SetVectorPageAddress(uint32_t u32PageAddress)
{
    if (Isp_IsIapConfigurationValid() == FALSE)
    {
        return -1;
    }
    FMC_CLR_FAIL_FLAG();
    if (FMC_SetVectorPageAddr(u32PageAddress) != 0)
    {
        return -1;
    }
    if (FMC_GET_FAIL_FLAG() != 0UL)
    {
        FMC_CLR_FAIL_FLAG();
        return -1;
    }
    if (FMC_GetVECMAP() !=
        (u32PageAddress & FMC_ISPSTS_VECMAP_Msk))
    {
        return -1;
    }
    return 0;
}

static void Isp_ResetToBootloaderCheck(void)
{
    uint32_t saved_primask;

    SYS_UnlockReg();
    FMC->ISPCTL |= FMC_ISPCTL_ISPEN_Msk;
    saved_primask = __get_PRIMASK();
    __set_PRIMASK(1u);
    if (Isp_SetVectorPageAddress(APROM_BOOTLOADER_START) != 0)
    {
        __set_PRIMASK(saved_primask);
        return;
    }
    __DSB();
    __ISB();
    SYS->IPRST0 = SYS_IPRST0_CPURST_Msk;

    while (1);
}

static uint8_t Isp_IsApplicationRange(uint32_t start, uint32_t size)
{
    if (size == 0u)
    {
        return 0u;
    }
    if (start < APROM_APPLICATION_START)
    {
        return 0u;
    }
    if (start >= APROM_APPLICATION_END)
    {
        return 0u;
    }
    if (size > (APROM_APPLICATION_END - start))
    {
        return 0u;
    }
    return 1u;
}

int ParseCmd(unsigned char *buffer, uint8_t len)
{
    static uint32_t StartAddress;
    static uint32_t StartAddress_bak;
    static uint32_t TotalLen;
    static uint32_t TotalLen_bak;
    static uint32_t LastDataLen;
    static uint8_t UpdateActive;
    uint8_t *response;
    uint16_t lcksum;
    uint16_t u16RequestChecksum;
    uint32_t lcmd;
    uint32_t u32RequestPackNo;
    uint32_t srclen;
    uint32_t PageAddress;
    uint32_t PreviousAddress;
    uint32_t PageEnd;
    uint32_t RequestedAddress;
    uint32_t RequestedLength;
    int i32Result;
    uint8_t u8CommandOk;
    unsigned char au8ExpectedData[56];
    unsigned char *pSrc;

    if ((buffer == NULL) || (len < 8u))
    {
        return -1;
    }

    u16RequestChecksum = Checksum(buffer, len);
    u32RequestPackNo = inpw(buffer + 4);
    response = response_buff;
    u8CommandOk = TRUE;
    memset(response, 0, sizeof(response_buff));
    pSrc = buffer;
    srclen = len;
    lcmd = inpw(pSrc);
    pSrc += 8;
    srclen -= 8;

    if (lcmd == CMD_SYNC_PACKNO)
    {
        UpdateActive = FALSE;
        TotalLen = 0UL;
        LastDataLen = 0UL;
    }

    if (lcmd == CMD_GET_FWVER)
    {
        response[8] = FW_VERSION;
    }
    else if (lcmd == CMD_GET_DEVICEID)
    {
        outpw(response + 8, SYS->PDID);
        goto out;
    }
    else if ((lcmd == CMD_RUN_APROM) || (lcmd == CMD_RUN_LDROM) || (lcmd == CMD_RESET))
    {
        UpdateActive = FALSE;
        Isp_ResetToBootloaderCheck();
    }
    else if (lcmd == CMD_CONNECT)
    {
        UpdateActive = FALSE;
        TotalLen = 0UL;
        LastDataLen = 0UL;
        outpw(response + 8, APROM_APPLICATION_SIZE);
        outpw(response + 12, APROM_APPLICATION_START);
        outpw(response + 16, APROM_APPLICATION_END);
        outpw(response + 20, APROM_APPLICATION_CRC_ADDR);
        goto out;
    }
    else if (lcmd == CMD_ERASE_ALL)
    {
        UpdateActive = FALSE;
        TotalLen = 0UL;
        LastDataLen = 0UL;
        if (Isp_IsIapConfigurationValid() == FALSE)
        {
            u8CommandOk = FALSE;
            goto out;
        }
        i32Result = EraseAP(APROM_APPLICATION_START, APROM_APPLICATION_SIZE);
        if (i32Result == 0)
        {
            bUpdateApromCmd = TRUE;
        }
        else
        {
            u8CommandOk = FALSE;
        }
        goto out;
    }
    else if (lcmd == CMD_READ_CONFIG)
    {
        i32Result = ReadData(Config0, Config0 + 16,
                             (unsigned int *)(response + 8));
        if (i32Result != 0)
        {
            u8CommandOk = FALSE;
        }
        goto out;
    }
    else if ((lcmd == CMD_UPDATE_DATAFLASH) || (lcmd == CMD_UPDATE_CONFIG))
    {
        UpdateActive = FALSE;
        TotalLen = 0UL;
        LastDataLen = 0UL;
        u8CommandOk = FALSE;
        goto out;
    }
    else if (lcmd == CMD_UPDATE_APROM)
    {
        UpdateActive = FALSE;
        TotalLen = 0UL;
        LastDataLen = 0UL;
        if (Isp_IsIapConfigurationValid() == FALSE)
        {
            u8CommandOk = FALSE;
            goto out;
        }
        RequestedAddress = inpw(pSrc);
        RequestedLength = inpw(pSrc + 4);

        if (((RequestedAddress != 0UL) &&
             (RequestedAddress != APROM_APPLICATION_START)) ||
            (RequestedLength != APROM_APPLICATION_SIZE))
        {
            u8CommandOk = FALSE;
            goto out;
        }

        i32Result = EraseAP(APROM_APPLICATION_START,
                            APROM_APPLICATION_SIZE);
        if (i32Result != 0)
        {
            u8CommandOk = FALSE;
            goto out;
        }

        StartAddress = APROM_APPLICATION_START;
        TotalLen = APROM_APPLICATION_SIZE;
        StartAddress_bak = StartAddress;
        TotalLen_bak = TotalLen;
        UpdateActive = TRUE;
        bUpdateApromCmd = TRUE;
        pSrc += 8;
        srclen -= 8;
    }
    else if (lcmd == CMD_RESEND_PACKET)
    {
        if ((UpdateActive == FALSE) || (LastDataLen == 0UL) ||
            (StartAddress < (APROM_APPLICATION_START + LastDataLen)))
        {
            u8CommandOk = FALSE;
            goto out;
        }

        PreviousAddress = StartAddress - LastDataLen;
        if (Isp_IsApplicationRange(PreviousAddress, LastDataLen) == 0u)
        {
            u8CommandOk = FALSE;
            goto out;
        }

        PageAddress = PreviousAddress & ~(FMC_FLASH_PAGE_SIZE - 1UL);
        PageEnd = PageAddress + FMC_FLASH_PAGE_SIZE;

        if ((PageAddress < APROM_APPLICATION_START) ||
            (PageEnd > APROM_APPLICATION_END))
        {
            u8CommandOk = FALSE;
            goto out;
        }

        if (PreviousAddress > PageAddress)
        {
            i32Result = ReadData(PageAddress, PreviousAddress,
                                 (unsigned int *)aprom_buf);
            if (i32Result != 0)
            {
                u8CommandOk = FALSE;
                goto out;
            }
        }

        i32Result = FMC_Erase_User(PageAddress);
        if (i32Result != 0)
        {
            u8CommandOk = FALSE;
            goto out;
        }

        if (PreviousAddress > PageAddress)
        {
            i32Result = WriteData(PageAddress, PreviousAddress,
                                  (unsigned int *)aprom_buf);
            if (i32Result != 0)
            {
                u8CommandOk = FALSE;
                goto out;
            }
        }

        if ((PreviousAddress + LastDataLen) > PageEnd)
        {
            if ((PageEnd + FMC_FLASH_PAGE_SIZE) > APROM_APPLICATION_END)
            {
                u8CommandOk = FALSE;
                goto out;
            }
            i32Result = FMC_Erase_User(PageEnd);
            if (i32Result != 0)
            {
                u8CommandOk = FALSE;
                goto out;
            }
        }

        StartAddress = PreviousAddress;
        TotalLen += LastDataLen;
        LastDataLen = 0UL;
        goto out;
    }
    else if ((lcmd != 0UL) && (lcmd != CMD_SYNC_PACKNO))
    {
        UpdateActive = FALSE;
        TotalLen = 0UL;
        LastDataLen = 0UL;
        u8CommandOk = FALSE;
        goto out;
    }

    if ((UpdateActive != FALSE) &&
        ((lcmd == CMD_UPDATE_APROM) || (lcmd == 0UL)) &&
        (TotalLen != 0UL))
    {
        if (TotalLen < srclen)
        {
            srclen = TotalLen;
        }

        if (Isp_IsApplicationRange(StartAddress, srclen) == 0u)
        {
            UpdateActive = FALSE;
            u8CommandOk = FALSE;
            goto out;
        }

        if (srclen > sizeof(au8ExpectedData))
        {
            UpdateActive = FALSE;
            u8CommandOk = FALSE;
            goto out;
        }
        memcpy(au8ExpectedData, pSrc, srclen);
        i32Result = WriteData(StartAddress, StartAddress + srclen,
                              (unsigned int *)pSrc);
        if (i32Result != 0)
        {
            UpdateActive = FALSE;
            u8CommandOk = FALSE;
            goto out;
        }
        memset(pSrc, 0, srclen);
        i32Result = ReadData(StartAddress, StartAddress + srclen,
                             (unsigned int *)pSrc);
        if (i32Result != 0)
        {
            UpdateActive = FALSE;
            u8CommandOk = FALSE;
            goto out;
        }
        if (memcmp(pSrc, au8ExpectedData, srclen) != 0)
        {
            UpdateActive = FALSE;
            u8CommandOk = FALSE;
            goto out;
        }

        StartAddress += srclen;
        TotalLen -= srclen;
        LastDataLen =  srclen;

        if (TotalLen == 0u)
        {
            i32Result = CalCheckSum(StartAddress_bak, TotalLen_bak,
                                    &lcksum);
            if (i32Result != 0)
            {
                UpdateActive = FALSE;
                u8CommandOk = FALSE;
                goto out;
            }
            outps(response + 8, lcksum);
        }
    }

out:
    lcksum = u16RequestChecksum;
    if (u8CommandOk == FALSE)
    {
        lcksum ^= 0xFFFFu;
    }
    outps(response, lcksum);
    outpw(response + 4, u32RequestPackNo + 1UL);
    return 0;
}
