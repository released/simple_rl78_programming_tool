/***************************************************************************//**
 * @file     main.c
 * @brief    M032 TEST BOARD UART IAP bootloader entry point
 *
 * @note
 * SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/
#include <stdint.h>
#include "targetdev.h"
#include "isp_user.h"
#include "iap_layout.h"

#if FMC_FLASH_PAGE_SIZE != M032_IAP_FLASH_PAGE_SIZE
#error "M032 TEST BOARD IAP layout requires a 512-byte FMC erase page"
#endif

#define TEST_BOARD_ID                 0x3142544DUL
#define UART_PACKET_SIZE              64u
#define UART_PACKET_TIMEOUT_MS        100UL
#define UART_ACTIVITY_LED_MS          80UL
#define TEST_BOARD_CBS_APROM_WITH_IAP (2UL << FMC_ISPSTS_CBS_Pos)

#ifdef __ICCARM__
#pragma data_alignment=4
uint8_t usb_rcvbuf[UART_PACKET_SIZE];
#else
uint8_t usb_rcvbuf[UART_PACKET_SIZE] __attribute__((aligned(4)));
#endif

static volatile uint32_t s_u32Milliseconds;

void SysTick_Handler(void)
{
    s_u32Milliseconds++;
}

static uint32_t ReadLe32(const uint8_t *data)
{
    uint32_t value;

    value = (uint32_t)data[0];
    value |= (uint32_t)data[1] << 8u;
    value |= (uint32_t)data[2] << 16u;
    value |= (uint32_t)data[3] << 24u;
    return value;
}

static void WriteLe32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFu);
    data[1] = (uint8_t)((value >> 8u) & 0xFFu);
    data[2] = (uint8_t)((value >> 16u) & 0xFFu);
    data[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint8_t IsKnownCommand(uint32_t command)
{
    if ((command == 0UL) ||
        (command == CMD_UPDATE_APROM) ||
        (command == CMD_READ_CONFIG) ||
        (command == CMD_ERASE_ALL) ||
        (command == CMD_SYNC_PACKNO) ||
        (command == CMD_GET_FWVER) ||
        (command == CMD_RUN_APROM) ||
        (command == CMD_RESET) ||
        (command == CMD_CONNECT) ||
        (command == CMD_GET_DEVICEID) ||
        (command == CMD_RESEND_PACKET))
    {
        return TRUE;
    }
    return FALSE;
}

static void Board_SetLeds(uint8_t red_on, uint8_t green_on)
{
    PA11 = (red_on != FALSE) ? 0 : 1;
    PA10 = (green_on != FALSE) ? 0 : 1;
}

static uint8_t Board_IsButtonPressed(void)
{
    return (PC2 == 0) ? TRUE : FALSE;
}

static void Board_Init(void)
{
    SYS->GPF_MFPL =
        (SYS->GPF_MFPL &
         ~(SYS_GPF_MFPL_PF2MFP_Msk | SYS_GPF_MFPL_PF3MFP_Msk)) |
        SYS_GPF_MFPL_PF2MFP_UART0_RXD |
        SYS_GPF_MFPL_PF3MFP_UART0_TXD;
    SYS->GPA_MFPH =
        (SYS->GPA_MFPH &
         ~(SYS_GPA_MFPH_PA10MFP_Msk | SYS_GPA_MFPH_PA11MFP_Msk)) |
        SYS_GPA_MFPH_PA10MFP_GPIO |
        SYS_GPA_MFPH_PA11MFP_GPIO;
    SYS->GPC_MFPL =
        (SYS->GPC_MFPL & ~SYS_GPC_MFPL_PC2MFP_Msk) |
        SYS_GPC_MFPL_PC2MFP_GPIO;

    PA->MODE =
        (PA->MODE &
         ~(GPIO_MODE_MODE10_Msk | GPIO_MODE_MODE11_Msk)) |
        (GPIO_MODE_OUTPUT << GPIO_MODE_MODE10_Pos) |
        (GPIO_MODE_OUTPUT << GPIO_MODE_MODE11_Pos);
    PC->MODE =
        (PC->MODE & ~GPIO_MODE_MODE2_Msk) |
        (GPIO_MODE_INPUT << GPIO_MODE_MODE2_Pos);
    Board_SetLeds(FALSE, FALSE);
    UART_Open(UART0, 115200UL);
}

static void SYS_Init(void)
{
    SYS_UnlockReg();
    CLK->PWRCTL |= CLK_PWRCTL_HIRCEN_Msk;
    while ((CLK->STATUS & CLK_STATUS_HIRCSTB_Msk) == 0UL)
    {
    }

    CLK_SetHCLK(CLK_CLKSEL0_HCLKSEL_HIRC, CLK_CLKDIV0_HCLK(1));
    CLK_EnableModuleClock(UART0_MODULE);
    CLK_SetModuleClock(
        UART0_MODULE, CLK_CLKSEL1_UART0SEL_HIRC, CLK_CLKDIV0_UART0(1));
    CLK_EnableModuleClock(CRC_MODULE);
    CLK_EnableModuleClock(ISP_MODULE);
    SystemCoreClockUpdate();
}

static uint32_t Iap_ReadWord(uint32_t address)
{
    uint32_t data;

    data = 0xFFFFFFFFUL;
    (void)FMC_Read_User(address, (unsigned int *)&data);
    return data;
}

static uint8_t Iap_IsConfigurationValid(void)
{
    if ((FMC->ISPSTS & FMC_ISPSTS_CBS_Msk) !=
        TEST_BOARD_CBS_APROM_WITH_IAP)
    {
        return FALSE;
    }
    return TRUE;
}

static uint8_t Iap_IsApplicationPresent(void)
{
    uint32_t stack;
    uint32_t reset;
    uint32_t reset_address;
    uint32_t crc;

    stack = Iap_ReadWord(APROM_APPLICATION_START);
    reset = Iap_ReadWord(APROM_APPLICATION_START + 4UL);
    crc = Iap_ReadWord(APROM_APPLICATION_CRC_ADDR);
    reset_address = reset & 0xFFFFFFFEUL;

    if ((stack == 0xFFFFFFFFUL) ||
        (reset == 0xFFFFFFFFUL) ||
        (crc == 0xFFFFFFFFUL))
    {
        return FALSE;
    }
    if ((stack < M032_IAP_SRAM_START) ||
        (stack > M032_IAP_SRAM_END) ||
        ((stack & 0x3UL) != 0UL))
    {
        return FALSE;
    }
    if (((reset & 0x1UL) == 0UL) ||
        (reset_address < APROM_APPLICATION_START) ||
        (reset_address >= APROM_APPLICATION_CRC_PAGE))
    {
        return FALSE;
    }
    return TRUE;
}

static uint32_t Iap_CalculateApplicationCrc32(void)
{
    uint32_t address;
    uint32_t data;

    CRC_Open(CRC_32,
             (CRC_WDATA_RVS | CRC_CHECKSUM_RVS | CRC_CHECKSUM_COM),
             0xFFFFFFFFUL,
             CRC_WDATA_32);
    for (address = APROM_APPLICATION_START;
         address < APROM_APPLICATION_CRC_ADDR;
         address += 4UL)
    {
        data = Iap_ReadWord(address);
        CRC_WRITE_DATA(data);
    }
    return CRC_GetChecksum();
}

static uint8_t Iap_VerifyApplication(void)
{
    FMC->ISPCTL |= FMC_ISPCTL_ISPEN_Msk;
    FMC_CLR_FAIL_FLAG();
    if (Iap_IsApplicationPresent() == FALSE)
    {
        FMC_CLR_FAIL_FLAG();
        return FALSE;
    }
    if (Iap_CalculateApplicationCrc32() !=
        Iap_ReadWord(APROM_APPLICATION_CRC_ADDR))
    {
        FMC_CLR_FAIL_FLAG();
        return FALSE;
    }
    if (FMC_GET_FAIL_FLAG() != 0UL)
    {
        FMC_CLR_FAIL_FLAG();
        return FALSE;
    }
    return TRUE;
}

static void Iap_JumpToApplication(void)
{
    uint32_t saved_primask;

    saved_primask = __get_PRIMASK();
    __set_PRIMASK(1u);
    FMC_CLR_FAIL_FLAG();
    if ((FMC_SetVectorPageAddr(APROM_APPLICATION_START) != 0) ||
        (FMC_GET_FAIL_FLAG() != 0UL) ||
        (FMC_GetVECMAP() !=
         (APROM_APPLICATION_START & FMC_ISPSTS_VECMAP_Msk)))
    {
        FMC_CLR_FAIL_FLAG();
        __set_PRIMASK(saved_primask);
        return;
    }
    __DSB();
    __ISB();
    SYS->IPRST0 = SYS_IPRST0_CPURST_Msk;
    while (1)
    {
    }
}

static void Iap_ResetToApplication(void)
{
    __set_PRIMASK(1u);
    Board_SetLeds(FALSE, TRUE);
    SYS_UnlockReg();
    /*
     * Re-enter through the resident bootloader with clean peripheral state.
     * The next boot verifies the CRC again and then maps the application
     * vectors through Iap_JumpToApplication().
     */
    SYS->IPRST0 = SYS_IPRST0_CHIPRST_Msk;
    while (1)
    {
    }
}

static void Uart_WritePacket(const uint8_t *packet)
{
    uint32_t index;

    for (index = 0UL; index < UART_PACKET_SIZE; ++index)
    {
        while (UART_IS_TX_FULL(UART0) != 0UL)
        {
        }
        UART_WRITE(UART0, packet[index]);
    }
    while (UART_IS_TX_EMPTY(UART0) == 0UL)
    {
    }
}

static uint8_t Uart_ReadPacket(uint8_t *packet)
{
    static uint32_t length;
    static uint32_t last_byte_ms;
    uint32_t now;
    uint32_t command;
    uint32_t index;

    now = s_u32Milliseconds;
    if ((length != 0UL) &&
        ((now - last_byte_ms) >= UART_PACKET_TIMEOUT_MS))
    {
        length = 0UL;
    }

    while (UART_IS_RX_READY(UART0) != 0UL)
    {
        if (length < UART_PACKET_SIZE)
        {
            packet[length] = (uint8_t)UART_READ(UART0);
            length++;
        }
        else
        {
            (void)UART_READ(UART0);
            length = 0UL;
        }
        last_byte_ms = s_u32Milliseconds;

        while (length >= 4UL)
        {
            command = ReadLe32(packet);
            if (IsKnownCommand(command) != FALSE)
            {
                break;
            }
            for (index = 1UL; index < length; ++index)
            {
                packet[index - 1UL] = packet[index];
            }
            length--;
        }

        if (length == UART_PACKET_SIZE)
        {
            length = 0UL;
            return TRUE;
        }
    }
    return FALSE;
}

int32_t main(void)
{
    uint32_t command;
    uint32_t activity_until_ms;

    activity_until_ms = 0UL;
    SYS_Init();
    SYS_UnlockReg();
    Board_Init();
    (void)SysTick_Config(SystemCoreClock / 1000UL);

    FMC->ISPCTL |= FMC_ISPCTL_ISPEN_Msk | FMC_ISPCTL_APUEN_Msk;
    g_apromSize = GetApromSize();
    GetDataFlashInfo(&g_dataFlashAddr, &g_dataFlashSize);

    Board_SetLeds(TRUE, FALSE);
    if ((Iap_IsConfigurationValid() != FALSE) &&
        (Board_IsButtonPressed() == FALSE) &&
        (Iap_VerifyApplication() != FALSE))
    {
        Board_SetLeds(FALSE, TRUE);
        Iap_JumpToApplication();
    }

    while (1)
    {
        if ((int32_t)(activity_until_ms - s_u32Milliseconds) > 0)
        {
            Board_SetLeds(FALSE, TRUE);
        }
        else
        {
            Board_SetLeds(TRUE, FALSE);
        }
        if (Uart_ReadPacket((uint8_t *)usb_rcvbuf) != FALSE)
        {
            command = ReadLe32((const uint8_t *)usb_rcvbuf);
            Board_SetLeds(FALSE, TRUE);
            if (command == CMD_RUN_APROM)
            {
                if ((Iap_IsConfigurationValid() != FALSE) &&
                    (Iap_VerifyApplication() != FALSE))
                {
                    Iap_ResetToApplication();
                }
                Board_SetLeds(TRUE, FALSE);
                continue;
            }
            (void)ParseCmd((uint8_t *)usb_rcvbuf, UART_PACKET_SIZE);
            if (command == CMD_CONNECT)
            {
                WriteLe32(response_buff + 24u, TEST_BOARD_ID);
            }
            Uart_WritePacket(response_buff);
            activity_until_ms =
                s_u32Milliseconds + UART_ACTIVITY_LED_MS;
        }
    }
}
