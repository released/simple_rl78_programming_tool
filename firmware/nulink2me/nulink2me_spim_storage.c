#include "nulink2me_spim_storage.h"

#include <string.h>

#include "NuMicro.h"
#include "rl78_programmer/plp_contract.h"

#define W25Q16_MANUFACTURER_ID 0xEFu
#define W25Q16_MEMORY_TYPE 0x40u
#define W25Q16_CAPACITY_ID 0x15u
#define VERIFY_BUFFER_SIZE 64u

static uint32_t ValidateRange(uint32_t offset, uint32_t length)
{
    if (offset > NULINK2ME_W25Q16_CAPACITY ||
        length > NULINK2ME_W25Q16_CAPACITY - offset)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    return PLP_OK;
}

static uint32_t StorageRead(void *context, uint32_t offset,
                            uint8_t *data, uint32_t length)
{
    Nulink2MeSpimStorage *storage;
    uint32_t status;
    storage = (Nulink2MeSpimStorage *)context;
    if (storage == 0 || storage->initialized == 0u ||
        (data == 0 && length != 0u))
    {
        return PLP_ERR_STORAGE_WRITE;
    }
    status = ValidateRange(offset, length);
    if (status != PLP_OK || length == 0u)
    {
        return status;
    }
    SPIM_IO_Read(offset, 0, length, data, OPCODE_FAST_READ, 1u, 1u, 1u, 1);
    return PLP_OK;
}

static uint32_t VerifyErased(uint32_t offset, uint32_t length)
{
    uint8_t buffer[VERIFY_BUFFER_SIZE];
    uint32_t cursor;
    uint32_t amount;
    uint32_t index;
    cursor = 0u;
    while (cursor < length)
    {
        amount = length - cursor;
        if (amount > VERIFY_BUFFER_SIZE)
        {
            amount = VERIFY_BUFFER_SIZE;
        }
        SPIM_IO_Read(offset + cursor, 0, amount, buffer,
                     OPCODE_FAST_READ, 1u, 1u, 1u, 1);
        for (index = 0u; index < amount; ++index)
        {
            if (buffer[index] != 0xFFu)
            {
                return PLP_ERR_STORAGE_WRITE;
            }
        }
        cursor += amount;
    }
    return PLP_OK;
}

static uint32_t StorageErase(void *context, uint32_t offset, uint32_t length)
{
    Nulink2MeSpimStorage *storage;
    uint32_t status;
    uint32_t cursor;
    storage = (Nulink2MeSpimStorage *)context;
    if (storage == 0 || storage->initialized == 0u)
    {
        return PLP_ERR_STORAGE_WRITE;
    }
    status = ValidateRange(offset, length);
    if (status != PLP_OK)
    {
        return status;
    }
    if ((offset % NULINK2ME_W25Q16_ERASE_SIZE) != 0u ||
        (length % NULINK2ME_W25Q16_ERASE_SIZE) != 0u)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    cursor = 0u;
    while (cursor < length)
    {
        SPIM_EraseBlock(offset + cursor, 0, OPCODE_SE_4K, 1u, 1);
        status = VerifyErased(offset + cursor,
                              NULINK2ME_W25Q16_ERASE_SIZE);
        if (status != PLP_OK)
        {
            return status;
        }
        cursor += NULINK2ME_W25Q16_ERASE_SIZE;
    }
    return PLP_OK;
}

static uint32_t VerifyWritten(uint32_t offset, const uint8_t *data,
                              uint32_t length)
{
    uint8_t buffer[VERIFY_BUFFER_SIZE];
    uint32_t cursor;
    uint32_t amount;
    cursor = 0u;
    while (cursor < length)
    {
        amount = length - cursor;
        if (amount > VERIFY_BUFFER_SIZE)
        {
            amount = VERIFY_BUFFER_SIZE;
        }
        SPIM_IO_Read(offset + cursor, 0, amount, buffer,
                     OPCODE_FAST_READ, 1u, 1u, 1u, 1);
        if (memcmp(buffer, data + cursor, amount) != 0)
        {
            return PLP_ERR_STORAGE_WRITE;
        }
        cursor += amount;
    }
    return PLP_OK;
}

static uint32_t StorageWrite(void *context, uint32_t offset,
                             const uint8_t *data, uint32_t length)
{
    Nulink2MeSpimStorage *storage;
    uint32_t status;
    uint32_t cursor;
    uint32_t amount;
    uint32_t page_remaining;
    storage = (Nulink2MeSpimStorage *)context;
    if (storage == 0 || storage->initialized == 0u ||
        (data == 0 && length != 0u))
    {
        return PLP_ERR_STORAGE_WRITE;
    }
    status = ValidateRange(offset, length);
    if (status != PLP_OK || length == 0u)
    {
        return status;
    }
    cursor = 0u;
    while (cursor < length)
    {
        page_remaining = NULINK2ME_W25Q16_PAGE_SIZE -
                         ((offset + cursor) % NULINK2ME_W25Q16_PAGE_SIZE);
        amount = length - cursor;
        if (amount > page_remaining)
        {
            amount = page_remaining;
        }
        SPIM_IO_Write(offset + cursor, 0, amount,
                      (uint8_t *)(data + cursor), OPCODE_PP, 1u, 1u, 1u);
        status = VerifyWritten(offset + cursor, data + cursor, amount);
        if (status != PLP_OK)
        {
            return status;
        }
        cursor += amount;
    }
    return PLP_OK;
}

static uint32_t StorageSync(void *context)
{
    Nulink2MeSpimStorage *storage;
    storage = (Nulink2MeSpimStorage *)context;
    return storage != 0 && storage->initialized != 0u ?
        PLP_OK : PLP_ERR_STORAGE_WRITE;
}

static void ConfigureSpimPins(void)
{
    SYS_UnlockReg();
    CLK_EnableModuleClock(SPIM_MODULE);
    SYS->GPC_MFPL =
        (SYS->GPC_MFPL & ~(SYS_GPC_MFPL_PC0MFP_Msk |
                           SYS_GPC_MFPL_PC1MFP_Msk |
                           SYS_GPC_MFPL_PC2MFP_Msk |
                           SYS_GPC_MFPL_PC3MFP_Msk)) |
        SYS_GPC_MFPL_PC0MFP_SPIM_MOSI |
        SYS_GPC_MFPL_PC1MFP_SPIM_MISO |
        SYS_GPC_MFPL_PC2MFP_SPIM_CLK |
        SYS_GPC_MFPL_PC3MFP_SPIM_SS;
    PC->SMTEN |= GPIO_SMTEN_SMTEN2_Msk;
    PC->SLEWCTL = (PC->SLEWCTL & 0xFFFFF000ul) |
        (0x1ul << GPIO_SLEWCTL_HSREN0_Pos) |
        (0x1ul << GPIO_SLEWCTL_HSREN1_Pos) |
        (0x1ul << GPIO_SLEWCTL_HSREN2_Pos) |
        (0x1ul << GPIO_SLEWCTL_HSREN3_Pos);
    SYS_LockReg();
}

uint32_t Nulink2MeSpimStorage_Init(Nulink2MeSpimStorage *storage)
{
    if (storage == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(storage, 0, sizeof(*storage));
    ConfigureSpimPins();
    SPIM_SET_CLOCK_DIVIDER(2u);
    SPIM_SET_RXCLKDLY_RDDLYSEL(0u);
    SPIM_SET_RXCLKDLY_RDEDGE();
    SPIM_SET_DCNUM(8u);
    if (SPIM_InitFlash(1) != 0)
    {
        return PLP_ERR_STORAGE_WRITE;
    }
    SPIM_ReadJedecId(storage->jedec_id, sizeof(storage->jedec_id), 1u);
    if (storage->jedec_id[0] != W25Q16_MANUFACTURER_ID ||
        storage->jedec_id[1] != W25Q16_MEMORY_TYPE ||
        storage->jedec_id[2] != W25Q16_CAPACITY_ID)
    {
        return PLP_ERR_STORAGE_WRITE;
    }
    SPIM_WinbondUnlock(1);
    storage->backend.context = storage;
    storage->backend.capacity = NULINK2ME_W25Q16_CAPACITY;
    storage->backend.erase_size = NULINK2ME_W25Q16_ERASE_SIZE;
    storage->backend.read = StorageRead;
    storage->backend.erase = StorageErase;
    storage->backend.write = StorageWrite;
    storage->backend.sync = StorageSync;
    storage->initialized = 1u;
    return PLP_OK;
}
