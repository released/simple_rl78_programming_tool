#include "isp_user.h"

#include <stdio.h>

#define ISP_CBS_APROM_WITH_IAP (2UL << FMC_ISPSTS_CBS_Pos)

__ALIGNED(4) uint8_t response_buff[64];
__ALIGNED(4) static uint8_t aprom_buf[FMC_FLASH_PAGE_SIZE];
uint32_t bUpdateApromCmd;
uint32_t g_apromSize;
uint32_t g_dataFlashAddr;
uint32_t g_dataFlashSize;

static uint16_t Checksum(const unsigned char *buffer, int length)
{
    int index;
    uint16_t checksum;
    checksum = 0u;
    for (index = 0; index < length; ++index)
    {
        checksum = (uint16_t)(checksum + buffer[index]);
    }
    return checksum;
}

static int CalculateFlashChecksum(uint32_t start, uint32_t length,
                                  uint16_t *flash_checksum)
{
    uint32_t offset;
    uint32_t chunk;
    uint16_t checksum;
    if (flash_checksum == 0)
    {
        return -1;
    }
    checksum = 0u;
    for (offset = 0UL; offset < length; offset += FMC_FLASH_PAGE_SIZE)
    {
        chunk = length - offset;
        if (chunk > FMC_FLASH_PAGE_SIZE)
        {
            chunk = FMC_FLASH_PAGE_SIZE;
        }
        if (ReadData(start + offset, start + offset + chunk,
                     (unsigned int *)aprom_buf) != 0)
        {
            return -1;
        }
        checksum = (uint16_t)(checksum +
                              Checksum(aprom_buf, (int)chunk));
    }
    *flash_checksum = checksum;
    return 0;
}

static uint8_t IsIapConfigurationValid(void)
{
    if ((FMC->ISPSTS & FMC_ISPSTS_CBS_Msk) !=
        ISP_CBS_APROM_WITH_IAP)
    {
        return 0u;
    }
    return ((FMC->ISPSTS & FMC_ISPSTS_MBS_Msk) != 0UL) ? 1u : 0u;
}

static int32_t SetVectorPageAddress(uint32_t page_address)
{
    if (IsIapConfigurationValid() == 0u)
    {
        return -1;
    }
    FMC_CLR_FAIL_FLAG();
    if (FMC_SetVectorPageAddr(page_address) != 0)
    {
        return -1;
    }
    if (FMC_GET_FAIL_FLAG() != 0UL)
    {
        FMC_CLR_FAIL_FLAG();
        return -1;
    }
    if (FMC_GetVECMAP() != (page_address & FMC_ISPSTS_VECMAP_Msk))
    {
        return -1;
    }
    return 0;
}

static void ResetToBootloaderCheck(void)
{
    uint32_t saved_primask;

    SYS_UnlockReg();
    FMC_Open();
    saved_primask = __get_PRIMASK();
    __set_PRIMASK(1u);
    if (SetVectorPageAddress(APROM_BOOTLOADER_START) != 0)
    {
        __set_PRIMASK(saved_primask);
        return;
    }
    __DSB();
    __ISB();
    SYS->IPRST0 = SYS_IPRST0_CPURST_Msk;
    for (;;)
    {
    }
}

static uint8_t IsApplicationRange(uint32_t start, uint32_t size)
{
    if (size == 0UL || start < APROM_APPLICATION_START ||
        start >= APROM_APPLICATION_END ||
        size > (APROM_APPLICATION_END - start))
    {
        return 0u;
    }
    return 1u;
}

int ParseCmd(unsigned char *buffer, uint8_t length)
{
    static uint32_t start_address;
    static uint32_t original_start;
    static uint32_t total_length;
    static uint32_t original_length;
    static uint32_t last_data_length;
    static uint8_t update_active;
    uint8_t *response;
    uint16_t checksum;
    uint16_t request_checksum;
    uint32_t command;
    uint32_t request_packet_number;
    uint32_t source_length;
    uint32_t page_address;
    uint32_t previous_address;
    uint32_t page_end;
    uint32_t requested_address;
    uint32_t requested_length;
    int result;
    uint8_t command_ok;
    unsigned char expected_data[56];
    unsigned char *source;

    if (buffer == 0 || length < 8u)
    {
        return -1;
    }
    request_checksum = Checksum(buffer, length);
    request_packet_number = inpw(buffer + 4);
    response = response_buff;
    command_ok = 1u;
    memset(response, 0, sizeof(response_buff));
    source = buffer;
    source_length = length;
    command = inpw(source);
    source += 8;
    source_length -= 8UL;

    if (command == CMD_SYNC_PACKNO)
    {
        update_active = 0u;
        total_length = 0UL;
        last_data_length = 0UL;
    }

    if (command == CMD_GET_FWVER)
    {
        response[8] = FW_VERSION;
    }
    else if (command == CMD_GET_DEVICEID)
    {
        outpw(response + 8, SYS->PDID);
        goto done;
    }
    else if (command == CMD_RUN_APROM || command == CMD_RUN_LDROM ||
             command == CMD_RESET)
    {
        update_active = 0u;
        ResetToBootloaderCheck();
    }
    else if (command == CMD_CONNECT)
    {
        update_active = 0u;
        total_length = 0UL;
        last_data_length = 0UL;
        outpw(response + 8, APROM_APPLICATION_SIZE);
        outpw(response + 12, APROM_APPLICATION_START);
        outpw(response + 16, APROM_APPLICATION_END);
        outpw(response + 20, APROM_APPLICATION_CRC_ADDR);
        goto done;
    }
    else if (command == CMD_ERASE_ALL)
    {
        update_active = 0u;
        total_length = 0UL;
        last_data_length = 0UL;
        if (IsIapConfigurationValid() == 0u)
        {
            command_ok = 0u;
            goto done;
        }
        if (EraseAP(APROM_APPLICATION_START, APROM_APPLICATION_SIZE) == 0)
        {
            bUpdateApromCmd = 1UL;
        }
        else
        {
            command_ok = 0u;
        }
        goto done;
    }
    else if (command == CMD_READ_CONFIG)
    {
        result = ReadData(Config0, Config0 + 16UL,
                          (unsigned int *)(response + 8));
        if (result != 0)
        {
            command_ok = 0u;
        }
        goto done;
    }
    else if (command == CMD_UPDATE_DATAFLASH ||
             command == CMD_UPDATE_CONFIG)
    {
        update_active = 0u;
        total_length = 0UL;
        last_data_length = 0UL;
        command_ok = 0u;
        goto done;
    }
    else if (command == CMD_UPDATE_APROM)
    {
        update_active = 0u;
        total_length = 0UL;
        last_data_length = 0UL;
        if (IsIapConfigurationValid() == 0u)
        {
            command_ok = 0u;
            goto done;
        }
        requested_address = inpw(source);
        requested_length = inpw(source + 4);
        if ((requested_address != 0UL &&
            requested_address != APROM_APPLICATION_START) ||
            requested_length != APROM_APPLICATION_SIZE)
        {
            command_ok = 0u;
            goto done;
        }
        result = EraseAP(APROM_APPLICATION_START, APROM_APPLICATION_SIZE);
        if (result != 0)
        {
            command_ok = 0u;
            goto done;
        }
        start_address = APROM_APPLICATION_START;
        total_length = APROM_APPLICATION_SIZE;
        original_start = start_address;
        original_length = total_length;
        update_active = 1u;
        bUpdateApromCmd = 1UL;
        source += 8;
        source_length -= 8UL;
    }
    else if (command == CMD_RESEND_PACKET)
    {
        if (update_active == 0u || last_data_length == 0UL ||
            start_address < APROM_APPLICATION_START + last_data_length)
        {
            command_ok = 0u;
            goto done;
        }
        previous_address = start_address - last_data_length;
        if (IsApplicationRange(previous_address, last_data_length) == 0u)
        {
            command_ok = 0u;
            goto done;
        }
        page_address = previous_address & ~(FMC_FLASH_PAGE_SIZE - 1UL);
        page_end = page_address + FMC_FLASH_PAGE_SIZE;
        if (page_address < APROM_APPLICATION_START ||
            page_end > APROM_APPLICATION_END)
        {
            command_ok = 0u;
            goto done;
        }
        if (previous_address > page_address &&
            ReadData(page_address, previous_address,
                     (unsigned int *)aprom_buf) != 0)
        {
            command_ok = 0u;
            goto done;
        }
        if (FMC_Erase_User(page_address) != 0)
        {
            command_ok = 0u;
            goto done;
        }
        if (previous_address > page_address &&
            WriteData(page_address, previous_address,
                      (unsigned int *)aprom_buf) != 0)
        {
            command_ok = 0u;
            goto done;
        }
        if (previous_address + last_data_length > page_end)
        {
            if (page_end + FMC_FLASH_PAGE_SIZE > APROM_APPLICATION_END ||
                FMC_Erase_User(page_end) != 0)
            {
                command_ok = 0u;
                goto done;
            }
        }
        start_address = previous_address;
        total_length += last_data_length;
        last_data_length = 0UL;
        goto done;
    }
    else if (command != 0UL && command != CMD_SYNC_PACKNO)
    {
        update_active = 0u;
        total_length = 0UL;
        last_data_length = 0UL;
        command_ok = 0u;
        goto done;
    }

    if (update_active != 0u &&
        (command == CMD_UPDATE_APROM || command == 0UL) &&
        total_length != 0UL)
    {
        if (total_length < source_length)
        {
            source_length = total_length;
        }
        if (IsApplicationRange(start_address, source_length) == 0u)
        {
            update_active = 0u;
            command_ok = 0u;
            goto done;
        }
        if (source_length > sizeof(expected_data))
        {
            update_active = 0u;
            command_ok = 0u;
            goto done;
        }
        memcpy(expected_data, source, source_length);
        if (WriteData(start_address, start_address + source_length,
                      (unsigned int *)source) != 0)
        {
            update_active = 0u;
            command_ok = 0u;
            goto done;
        }
        memset(source, 0, source_length);
        if (ReadData(start_address, start_address + source_length,
                     (unsigned int *)source) != 0)
        {
            update_active = 0u;
            command_ok = 0u;
            goto done;
        }
        if (memcmp(source, expected_data, source_length) != 0)
        {
            update_active = 0u;
            command_ok = 0u;
            goto done;
        }
        start_address += source_length;
        total_length -= source_length;
        last_data_length = source_length;
        if (total_length == 0UL)
        {
            result = CalculateFlashChecksum(original_start,
                                            original_length,
                                            &checksum);
            if (result != 0)
            {
                update_active = 0u;
                command_ok = 0u;
                goto done;
            }
            outps(response + 8, checksum);
        }
    }

done:
    checksum = request_checksum;
    if (command_ok == 0u)
    {
        checksum ^= 0xFFFFu;
    }
    outps(response, checksum);
    outpw(response + 4, request_packet_number + 1UL);
    return 0;
}
