#include "m032_numicro_fmc_storage.h"

#include <string.h>

static uint32_t PrepareFmcUpdate(void)
{
    SYS_UnlockReg();
    FMC_Open();
    FMC_ENABLE_AP_UPDATE();
    FMC_CLR_FAIL_FLAG();
    if ((FMC->ISPCTL &
         (FMC_ISPCTL_ISPEN_Msk | FMC_ISPCTL_APUEN_Msk)) !=
        (FMC_ISPCTL_ISPEN_Msk | FMC_ISPCTL_APUEN_Msk))
    {
        return PLP_ERR_STORAGE_WRITE;
    }
    return PLP_OK;
}

static uint32_t ErasePage(uint32_t address)
{
    uint32_t saved_primask;
    uint32_t status;
    uint32_t verify_address;
    status = PrepareFmcUpdate();
    if (status != PLP_OK)
    {
        return status;
    }
    saved_primask = __get_PRIMASK();
    __set_PRIMASK(1u);
    __DSB();
    __ISB();
    status = FMC_Erase(address) == 0 ? PLP_OK : PLP_ERR_STORAGE_WRITE;
    __DSB();
    __ISB();
    if (FMC_GET_FAIL_FLAG() != 0u)
    {
        FMC_CLR_FAIL_FLAG();
        status = PLP_ERR_STORAGE_WRITE;
    }
    __set_PRIMASK(saved_primask);
    if (status != PLP_OK)
    {
        return status;
    }
    for (verify_address = address;
         verify_address < address + FMC_FLASH_PAGE_SIZE;
         verify_address += 4u)
    {
        if (M32(verify_address) != 0xFFFFFFFFul)
        {
            return PLP_ERR_STORAGE_WRITE;
        }
    }
    return PLP_OK;
}

static uint32_t ProgramWord(uint32_t address, uint32_t value)
{
    uint32_t saved_primask;
    uint32_t status;
    status = PrepareFmcUpdate();
    if (status != PLP_OK)
    {
        return status;
    }
    saved_primask = __get_PRIMASK();
    __set_PRIMASK(1u);
    __DSB();
    __ISB();
    status = FMC_Write(address, value) == 0 ?
             PLP_OK : PLP_ERR_STORAGE_WRITE;
    __DSB();
    __ISB();
    if (FMC_GET_FAIL_FLAG() != 0u)
    {
        FMC_CLR_FAIL_FLAG();
        status = PLP_ERR_STORAGE_WRITE;
    }
    __set_PRIMASK(saved_primask);
    if (status != PLP_OK || M32(address) != value)
    {
        return PLP_ERR_STORAGE_WRITE;
    }
    return PLP_OK;
}

static uint32_t CheckRange(const M032NumicroFmcStorage *storage,
                           uint32_t offset, uint32_t length)
{
    if (storage == 0 || offset > storage->capacity ||
        length > storage->capacity - offset)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    return PLP_OK;
}

static uint32_t FmcRead(void *context, uint32_t offset,
                        uint8_t *data, uint32_t length)
{
    M032NumicroFmcStorage *storage;
    uint32_t address;
    uint32_t index;
    storage = (M032NumicroFmcStorage *)context;
    if (data == 0 || CheckRange(storage, offset, length) != PLP_OK)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    for (index = 0u; index < length; ++index)
    {
        address = storage->base_address + offset + index;
        /*
         * APROM is memory mapped. Do not use FMC_Read() here: ISPTRG is a
         * protected register and a board peripheral reset may legitimately
         * restore SYS write protection before target programming begins.
         */
        data[index] = M8(address);
    }
    return PLP_OK;
}

static uint32_t FmcErase(void *context, uint32_t offset, uint32_t length)
{
    M032NumicroFmcStorage *storage;
    uint32_t address;
    storage = (M032NumicroFmcStorage *)context;
    if (CheckRange(storage, offset, length) != PLP_OK ||
        (offset % FMC_FLASH_PAGE_SIZE) != 0u ||
        (length % FMC_FLASH_PAGE_SIZE) != 0u)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    for (address = storage->base_address + offset;
         address < storage->base_address + offset + length;
         address += FMC_FLASH_PAGE_SIZE)
    {
        if (ErasePage(address) != PLP_OK)
        {
            return PLP_ERR_STORAGE_WRITE;
        }
    }
    return PLP_OK;
}

static uint32_t FmcWrite(void *context, uint32_t offset,
                         const uint8_t *data, uint32_t length)
{
    M032NumicroFmcStorage *storage;
    uint32_t first_word;
    uint32_t last_address;
    uint32_t word_address;
    uint32_t current;
    uint32_t updated;
    uint32_t byte_address;
    uint32_t data_index;
    uint32_t shift;
    storage = (M032NumicroFmcStorage *)context;
    if (data == 0 || CheckRange(storage, offset, length) != PLP_OK)
    {
        return PLP_ERR_STORAGE_OFFSET;
    }
    if (length == 0u)
    {
        return PLP_OK;
    }
    first_word = (storage->base_address + offset) & ~3ul;
    last_address = storage->base_address + offset + length;
    for (word_address = first_word; word_address < last_address; word_address += 4u)
    {
        current = M32(word_address);
        updated = current;
        for (byte_address = word_address; byte_address < word_address + 4u;
             ++byte_address)
        {
            if (byte_address >= storage->base_address + offset &&
                byte_address < last_address)
            {
                data_index = byte_address - (storage->base_address + offset);
                shift = (byte_address & 3u) * 8u;
                updated = (updated & ~(0xFFul << shift)) |
                          ((uint32_t)data[data_index] << shift);
            }
        }
        if ((current & updated) != updated)
        {
            return PLP_ERR_STORAGE_WRITE;
        }
        if (updated != current &&
            ProgramWord(word_address, updated) != PLP_OK)
        {
            return PLP_ERR_STORAGE_WRITE;
        }
    }
    return PLP_OK;
}

static uint32_t FmcSync(void *context)
{
    (void)context;
    return PLP_OK;
}

uint32_t M032NumicroFmcStorage_Init(M032NumicroFmcStorage *storage,
                                    uint32_t base_address,
                                    uint32_t capacity,
                                    uint32_t firmware_end_address,
                                    uint32_t aprom_size)
{
    uint32_t aprom_end;
    if (storage == 0 || capacity == 0u ||
        (base_address % FMC_FLASH_PAGE_SIZE) != 0u ||
        (capacity % FMC_FLASH_PAGE_SIZE) != 0u ||
        base_address < firmware_end_address || base_address > aprom_size ||
        capacity > aprom_size - base_address)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    aprom_end = base_address + capacity;
    if (aprom_end < base_address)
    {
        return PLP_ERR_CONFIG_VALIDATION;
    }
    memset(storage, 0, sizeof(*storage));
    storage->base_address = base_address;
    storage->capacity = capacity;
    storage->backend.context = storage;
    storage->backend.capacity = capacity;
    storage->backend.erase_size = FMC_FLASH_PAGE_SIZE;
    storage->backend.read = FmcRead;
    storage->backend.erase = FmcErase;
    storage->backend.write = FmcWrite;
    storage->backend.sync = FmcSync;
    return PrepareFmcUpdate();
}
