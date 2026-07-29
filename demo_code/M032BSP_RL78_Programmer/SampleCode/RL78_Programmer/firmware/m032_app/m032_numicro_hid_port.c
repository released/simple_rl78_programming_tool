/* SPDX-License-Identifier: Apache-2.0 */
/* Endpoint handling is adapted from the Nuvoton M031 USB HID BSP sample. */
#include "m032_numicro_hid_port.h"

#include <string.h>

#define HID_GET_REPORT 0x01u
#define HID_GET_IDLE 0x02u
#define HID_GET_PROTOCOL 0x03u
#define HID_SET_REPORT 0x09u
#define HID_SET_IDLE 0x0Au
#define HID_SET_PROTOCOL 0x0Bu

static M032NumicroHidPort *g_port;
static uint8_t g_idle;
static uint8_t g_protocol;

static uint8_t CanSend(void *context)
{
    M032NumicroHidPort *port;
    port = (M032NumicroHidPort *)context;
    return port != 0 && port->in_busy == 0u ? 1u : 0u;
}

static uint32_t Send(void *context, const uint8_t report[PLP_REPORT_SIZE])
{
    M032NumicroHidPort *port;
    uint8_t *buffer;
    port = (M032NumicroHidPort *)context;
    if (port == 0 || report == 0 || port->in_busy != 0u)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    buffer = (uint8_t *)(USBD_BUF_BASE + USBD_GET_EP_BUF_ADDR(EP2));
    USBD_MemCopy(buffer, (void *)report, M032_HID_REPORT_SIZE);
    port->in_busy = 1u;
    USBD_SET_PAYLOAD_LEN(EP2, M032_HID_REPORT_SIZE);
    return PLP_OK;
}

void M032NumicroHidPort_GetOps(M032NumicroHidPort *port, M032UsbPortOps *ops)
{
    if (port == 0 || ops == 0)
    {
        return;
    }
    memset(ops, 0, sizeof(*ops));
    ops->context = port;
    ops->can_send = CanSend;
    ops->send = Send;
}

static void ConfigureEndpoints(void)
{
    USBD->STBUFSEG = 0u;
    USBD_CONFIG_EP(EP0, USBD_CFG_CSTALL | USBD_CFG_EPMODE_IN | 0u);
    USBD_SET_EP_BUF_ADDR(EP0, 8u);
    USBD_CONFIG_EP(EP1, USBD_CFG_CSTALL | USBD_CFG_EPMODE_OUT | 0u);
    USBD_SET_EP_BUF_ADDR(EP1, 8u);
    USBD_CONFIG_EP(EP2, USBD_CFG_EPMODE_IN | M032_HID_INT_IN_NUMBER);
    USBD_SET_EP_BUF_ADDR(EP2, M032_HID_EP2_BUFFER_BASE);
    USBD_CONFIG_EP(EP3, USBD_CFG_EPMODE_OUT | M032_HID_INT_OUT_NUMBER);
    USBD_SET_EP_BUF_ADDR(EP3, M032_HID_EP3_BUFFER_BASE);
    USBD_SET_PAYLOAD_LEN(EP3, M032_HID_REPORT_SIZE);
}

static void ClassRequest(void)
{
    uint8_t setup[8];
    USBD_GetSetupPacket(setup);
    if ((setup[0] & 0x80u) != 0u)
    {
        switch (setup[1])
        {
            case HID_GET_IDLE:
                USBD_PrepareCtrlIn(&g_idle, setup[6]);
                USBD_PrepareCtrlOut(0, 0u);
                break;
            case HID_GET_PROTOCOL:
                USBD_PrepareCtrlIn(&g_protocol, setup[6]);
                USBD_PrepareCtrlOut(0, 0u);
                break;
            case HID_GET_REPORT:
            default:
                USBD_SetStall(EP0);
                USBD_SetStall(EP1);
                break;
        }
    }
    else
    {
        switch (setup[1])
        {
            case HID_SET_REPORT:
                USBD_SET_DATA1(EP1);
                USBD_SET_PAYLOAD_LEN(EP1, 0u);
                break;
            case HID_SET_IDLE:
                g_idle = setup[3];
                USBD_SET_DATA1(EP0);
                USBD_SET_PAYLOAD_LEN(EP0, 0u);
                break;
            case HID_SET_PROTOCOL:
                g_protocol = setup[2];
                USBD_SET_DATA1(EP0);
                USBD_SET_PAYLOAD_LEN(EP0, 0u);
                break;
            default:
                USBD_SetStall(EP0);
                USBD_SetStall(EP1);
                break;
        }
    }
}

uint32_t M032NumicroHidPort_Init(M032NumicroHidPort *port,
                                 M032Application *application)
{
    if (port == 0 || application == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(port, 0, sizeof(*port));
    port->application = application;
    g_port = port;
    USBD_Open(&g_M032HidDeviceInfo, ClassRequest, 0);
    ConfigureEndpoints();
    USBD_Start();
    NVIC_EnableIRQ(USBD_IRQn);
    return PLP_OK;
}

void USBD_IRQHandler(void)
{
    uint32_t interrupt_status;
    uint32_t bus_state;
    uint8_t *buffer;
    uint32_t length;
    interrupt_status = USBD_GET_INT_FLAG();
    bus_state = USBD_GET_BUS_STATE();
    if ((interrupt_status & USBD_INTSTS_FLDET) != 0u)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_FLDET);
        if (USBD_IS_ATTACHED())
        {
            USBD_ENABLE_USB();
        }
        else
        {
            USBD_DISABLE_USB();
        }
    }
    if ((interrupt_status & USBD_INTSTS_BUS) != 0u)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_BUS);
        if ((bus_state & USBD_STATE_USBRST) != 0u)
        {
            USBD_ENABLE_USB();
            USBD_SwReset();
            if (g_port != 0)
            {
                g_port->in_busy = 0u;
            }
        }
        if ((bus_state & USBD_STATE_SUSPEND) != 0u)
        {
            USBD_DISABLE_PHY();
        }
        if ((bus_state & USBD_STATE_RESUME) != 0u)
        {
            USBD_ENABLE_USB();
        }
    }
    if ((interrupt_status & USBD_INTSTS_USB) == 0u)
    {
        return;
    }
    if ((interrupt_status & USBD_INTSTS_SETUP) != 0u)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_SETUP);
        USBD_STOP_TRANSACTION(EP0);
        USBD_STOP_TRANSACTION(EP1);
        USBD_ProcessSetupPacket();
    }
    if ((interrupt_status & USBD_INTSTS_EP0) != 0u)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_EP0);
        USBD_CtrlIn();
    }
    if ((interrupt_status & USBD_INTSTS_EP1) != 0u)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_EP1);
        USBD_CtrlOut();
    }
    if ((interrupt_status & USBD_INTSTS_EP2) != 0u)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_EP2);
        if (g_port != 0)
        {
            g_port->in_busy = 0u;
        }
    }
    if ((interrupt_status & USBD_INTSTS_EP3) != 0u)
    {
        USBD_CLR_INT_FLAG(USBD_INTSTS_EP3);
        buffer = (uint8_t *)(USBD_BUF_BASE + USBD_GET_EP_BUF_ADDR(EP3));
        length = USBD_GET_PAYLOAD_LEN(EP3);
        if (g_port != 0 && g_port->application != 0)
        {
            (void)M032Application_OnUsbOut(g_port->application, buffer, length);
        }
        USBD_SET_PAYLOAD_LEN(EP3, M032_HID_REPORT_SIZE);
    }
}
