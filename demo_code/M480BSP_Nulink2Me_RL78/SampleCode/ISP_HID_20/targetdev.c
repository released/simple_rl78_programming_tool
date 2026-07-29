#include "targetdev.h"

uint32_t GetApromSize(void)
{
    return APROM_APPLICATION_SIZE;
}

void GetDataFlashInfo(uint32_t *address, uint32_t *size)
{
    *address = APROM_APPLICATION_END;
    *size = 0UL;
}
