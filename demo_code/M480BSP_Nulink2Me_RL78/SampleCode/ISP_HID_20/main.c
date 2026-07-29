#include <stdio.h>

#include "NuMicro.h"
#include "hid_transfer.h"
#include "iap_layout.h"
#include "targetdev.h"

#define IAP_CBS_APROM_WITH_IAP (2UL << FMC_ISPSTS_CBS_Pos)

extern uint8_t bUsbDataReady;
void USBD20_IRQHandler(void);

uint32_t CLK_GetPLLClockFreq(void)
{
    return FREQ_192MHZ;
}

static void SystemInitialize(void)
{
    volatile uint32_t delay;
    SYS_UnlockReg();
    PF->MODE &= ~(GPIO_MODE_MODE2_Msk | GPIO_MODE_MODE3_Msk);
    CLK->PWRCTL |= CLK_PWRCTL_HIRCEN_Msk | CLK_PWRCTL_HXTEN_Msk;
    while ((CLK->STATUS & CLK_STATUS_HIRCSTB_Msk) == 0u)
    {
    }
    CLK->CLKSEL0 = (CLK->CLKSEL0 & ~CLK_CLKSEL0_HCLKSEL_Msk) |
                   CLK_CLKSEL0_HCLKSEL_HIRC;
    CLK->PLLCTL = CLK_PLLCTL_PD_Msk;
    CLK->PLLCTL = 0x8842EUL;
    while ((CLK->STATUS & CLK_STATUS_PLLSTB_Msk) == 0u)
    {
    }
    CLK->CLKDIV0 &= ~CLK_CLKDIV0_HCLKDIV_Msk;
    CLK->CLKSEL0 = (CLK->CLKSEL0 & ~CLK_CLKSEL0_HCLKSEL_Msk) |
                   CLK_CLKSEL0_HCLKSEL_PLL;
    CLK->PCLKDIV = CLK_PCLKDIV_APB0DIV_DIV2 |
                   CLK_PCLKDIV_APB1DIV_DIV2;
    SYS->USBPHY &= ~SYS_USBPHY_HSUSBROLE_Msk;
    SYS->USBPHY =
        (SYS->USBPHY & ~(SYS_USBPHY_HSUSBROLE_Msk |
                         SYS_USBPHY_HSUSBACT_Msk)) |
        SYS_USBPHY_HSUSBEN_Msk;
    for (delay = 0UL; delay < 0x1000UL; ++delay)
    {
    }
    SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;
    CLK->AHBCLK |= CLK_AHBCLK_HSUSBDCKEN_Msk |
                   CLK_AHBCLK_ISPCKEN_Msk |
                   CLK_AHBCLK_CRCCKEN_Msk;
    SystemCoreClockUpdate();
}

static uint32_t ReadWord(uint32_t address)
{
    uint32_t value;
    value = 0xFFFFFFFFUL;
    if (FMC_Read_User(address, &value) != 0)
    {
        return 0xFFFFFFFFUL;
    }
    return value;
}

static uint8_t IsIapConfigurationValid(void)
{
    if ((FMC->ISPSTS & FMC_ISPSTS_CBS_Msk) !=
        IAP_CBS_APROM_WITH_IAP)
    {
        return 0u;
    }
    if ((FMC->ISPSTS & FMC_ISPSTS_MBS_Msk) == 0UL)
    {
        return 0u;
    }
    return 1u;
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

static uint8_t IsApplicationPresent(void)
{
    uint32_t stack_pointer;
    uint32_t reset_vector;
    uint32_t reset_address;
    uint32_t stored_crc;
    stack_pointer = ReadWord(APROM_APPLICATION_START);
    reset_vector = ReadWord(APROM_APPLICATION_START + 4UL);
    stored_crc = ReadWord(APROM_APPLICATION_CRC_ADDR);
    reset_address = reset_vector & 0xFFFFFFFEUL;
    if (stack_pointer == 0xFFFFFFFFUL ||
        reset_vector == 0xFFFFFFFFUL || stored_crc == 0xFFFFFFFFUL)
    {
        return 0u;
    }
    if (stack_pointer < IAP_SRAM_START || stack_pointer > IAP_SRAM_END ||
        (stack_pointer & 3UL) != 0UL)
    {
        return 0u;
    }
    if ((reset_vector & 1UL) == 0UL ||
        reset_address < APROM_APPLICATION_START ||
        reset_address >= APROM_APPLICATION_CRC_PAGE)
    {
        return 0u;
    }
    return 1u;
}

static uint32_t CalculateApplicationCrc32(void)
{
    uint32_t address;
    uint32_t value;
    CRC_Open(CRC_32,
             CRC_WDATA_RVS | CRC_CHECKSUM_RVS | CRC_CHECKSUM_COM,
             0xFFFFFFFFUL, CRC_CPU_WDATA_32);
    for (address = APROM_APPLICATION_START;
         address < APROM_APPLICATION_CRC_ADDR; address += 4UL)
    {
        value = ReadWord(address);
        CRC_WRITE_DATA(value);
    }
    return CRC_GetChecksum();
}

static uint8_t VerifyApplication(void)
{
    uint32_t calculated_crc;
    uint32_t stored_crc;
    if (IsApplicationPresent() == 0u)
    {
        return 0u;
    }
    calculated_crc = CalculateApplicationCrc32();
    stored_crc = ReadWord(APROM_APPLICATION_CRC_ADDR);
    return calculated_crc == stored_crc ? 1u : 0u;
}

static uint8_t JumpToApplication(void)
{
    uint32_t saved_primask;

    SYS_UnlockReg();
    FMC_Open();
    saved_primask = __get_PRIMASK();
    __set_PRIMASK(1u);
    if (SetVectorPageAddress(APROM_APPLICATION_START) != 0)
    {
        __set_PRIMASK(saved_primask);
        return 0u;
    }
    __DSB();
    __ISB();
    SYS->IPRST0 = SYS_IPRST0_CPURST_Msk;
    for (;;)
    {
    }
    return 0u;
}

int32_t main(void)
{
    SystemInitialize();
    SYS_UnlockReg();
    FMC_Open();
    FMC_ENABLE_AP_UPDATE();
    g_apromSize = GetApromSize();
    GetDataFlashInfo(&g_dataFlashAddr, &g_dataFlashSize);

    if (IsIapConfigurationValid() != 0u && VerifyApplication() != 0u)
    {
        (void)JumpToApplication();
    }

    HSUSBD_Open(0, 0, 0);
    HID_Init();
    HSUSBD_CLR_SE0();

    for (;;)
    {
        USBD20_IRQHandler();
        if (bUsbDataReady != 0u)
        {
            (void)ParseCmd((uint8_t *)usb_rcvbuf, 64u);
            EPA_Handler();
            bUsbDataReady = 0u;
        }
    }
}
