/* Endpoint handling follows the Nuvoton M480 HSUSBD HID transfer sample. */
#include "nulink2me_hsusbd_port.h"

#include <string.h>

#define HID_GET_REPORT 0x01u
#define HID_GET_IDLE 0x02u
#define HID_GET_PROTOCOL 0x03u
#define HID_SET_REPORT 0x09u
#define HID_SET_IDLE 0x0Au
#define HID_SET_PROTOCOL 0x0Bu

static Nulink2MeHsusbdPort *g_port;
static uint8_t g_idle;
static uint8_t g_protocol;

static uint8_t CanSend(void *context)
{
    Nulink2MeHsusbdPort *port;
    port = (Nulink2MeHsusbdPort *)context;
    return port != 0 && port->in_busy == 0u ? 1u : 0u;
}

static uint32_t Send(void *context,
                     const uint8_t report[PLP_REPORT_SIZE])
{
    Nulink2MeHsusbdPort *port;
    uint32_t index;
    port = (Nulink2MeHsusbdPort *)context;
    if (port == 0 || report == 0 || port->in_busy != 0u)
    {
        return PLP_ERR_SYSTEM_RESOURCE;
    }
    memcpy(port->in_report, report, NULINK2ME_HID_REPORT_SIZE);
    for (index = 0u; index < NULINK2ME_HID_REPORT_SIZE; ++index)
    {
        HSUSBD->EP[EPA].EPDAT_BYTE = port->in_report[index];
    }
    port->in_busy = 1u;
    HSUSBD->EP[EPA].EPTXCNT = NULINK2ME_HID_REPORT_SIZE;
    return PLP_OK;
}

void Nulink2MeHsusbdPort_GetOps(Nulink2MeHsusbdPort *port,
                                Nulink2MeUsbPortOps *ops)
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
    HSUSBD_ENABLE_USB_INT(HSUSBD_GINTEN_USBIEN_Msk |
                          HSUSBD_GINTEN_CEPIEN_Msk |
                          HSUSBD_GINTEN_EPAIEN_Msk |
                          HSUSBD_GINTEN_EPBIEN_Msk);
    HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                          HSUSBD_BUSINTEN_RSTIEN_Msk |
                          HSUSBD_BUSINTEN_VBUSDETIEN_Msk |
                          HSUSBD_BUSINTEN_SUSPENDIEN_Msk);
    HSUSBD_SET_ADDR(0u);
    HSUSBD_SetEpBufAddr(CEP, NULINK2ME_HID_CEP_BUFFER_BASE,
                        NULINK2ME_HID_CEP_BUFFER_SIZE);
    HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk |
                          HSUSBD_CEPINTEN_STSDONEIEN_Msk);

    HSUSBD_SetEpBufAddr(EPA, NULINK2ME_HID_IN_BUFFER_BASE,
                        NULINK2ME_HID_ENDPOINT_BUFFER_SIZE);
    HSUSBD_SET_MAX_PAYLOAD(EPA, NULINK2ME_HID_REPORT_SIZE);
    HSUSBD_ConfigEp(EPA, NULINK2ME_HID_INT_IN_NUMBER,
                    HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_IN);
    HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_TXPKIEN_Msk);

    HSUSBD_SetEpBufAddr(EPB, NULINK2ME_HID_OUT_BUFFER_BASE,
                        NULINK2ME_HID_ENDPOINT_BUFFER_SIZE);
    HSUSBD_SET_MAX_PAYLOAD(EPB, NULINK2ME_HID_REPORT_SIZE);
    HSUSBD_ConfigEp(EPB, NULINK2ME_HID_INT_OUT_NUMBER,
                    HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_OUT);
    HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk |
                              HSUSBD_EPINTEN_BUFFULLIEN_Msk);
}

static void StallControlEndpoint(void)
{
    HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
}

static void CompleteControlOut(void)
{
    HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
    HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
    HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
}

static void ClassRequest(void)
{
    if ((gUsbCmd.bmRequestType & 0x80u) != 0u)
    {
        if (gUsbCmd.bRequest == HID_GET_IDLE)
        {
            HSUSBD_PrepareCtrlIn(&g_idle, 1u);
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        }
        else if (gUsbCmd.bRequest == HID_GET_PROTOCOL)
        {
            HSUSBD_PrepareCtrlIn(&g_protocol, 1u);
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        }
        else
        {
            StallControlEndpoint();
        }
        return;
    }
    if (gUsbCmd.bRequest == HID_SET_REPORT)
    {
        if (((gUsbCmd.wValue >> 8) & 0xFFu) == 3u)
        {
            CompleteControlOut();
        }
        else
        {
            StallControlEndpoint();
        }
    }
    else if (gUsbCmd.bRequest == HID_SET_IDLE)
    {
        g_idle = (uint8_t)(gUsbCmd.wValue >> 8);
        CompleteControlOut();
    }
    else if (gUsbCmd.bRequest == HID_SET_PROTOCOL)
    {
        g_protocol = (uint8_t)gUsbCmd.wValue;
        CompleteControlOut();
    }
    else if (gUsbCmd.bRequest == HID_GET_REPORT)
    {
        StallControlEndpoint();
    }
    else
    {
        StallControlEndpoint();
    }
}

static void VendorRequest(void)
{
    StallControlEndpoint();
}

uint32_t Nulink2MeHsusbdPort_Init(Nulink2MeHsusbdPort *port,
                                  Nulink2MeApplication *application)
{
    if (port == 0 || application == 0)
    {
        return PLP_ERR_SDK_ARGUMENT;
    }
    memset(port, 0, sizeof(*port));
    port->application = application;
    g_port = port;
    HSUSBD_Open(&g_Nulink2MeHidDeviceInfo, ClassRequest, 0);
    HSUSBD_SetVendorRequest(VendorRequest);
    ConfigureEndpoints();
    NVIC_SetPriority(USBD20_IRQn, 2u);
    NVIC_EnableIRQ(USBD20_IRQn);
    HSUSBD_Start();
    return PLP_OK;
}

static void ProcessBusInterrupt(uint32_t status)
{
    if ((status & HSUSBD_BUSINTSTS_RSTIF_Msk) != 0u)
    {
        HSUSBD_SwReset();
        HSUSBD_ResetDMA();
        HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EP_RSPCTL_FLUSH;
        HSUSBD->EP[EPB].EPRSPCTL = HSUSBD_EP_RSPCTL_FLUSH;
        ConfigureEndpoints();
        HSUSBD_SET_ADDR(0u);
        if (g_port != 0)
        {
            g_port->in_busy = 0u;
            if (g_port->application != 0)
            {
                Nulink2MeApplication_OnUsbDisconnected(
                    g_port->application);
            }
        }
        HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RSTIF_Msk);
        HSUSBD_CLR_CEP_INT_FLAG(0x1FFCu);
    }
    if ((status & HSUSBD_BUSINTSTS_RESUMEIF_Msk) != 0u)
    {
        HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RESUMEIF_Msk);
    }
    if ((status & HSUSBD_BUSINTSTS_SUSPENDIF_Msk) != 0u)
    {
        HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SUSPENDIF_Msk);
    }
    if ((status & HSUSBD_BUSINTSTS_HISPDIF_Msk) != 0u)
    {
        HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_HISPDIF_Msk);
    }
    if ((status & HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk) != 0u)
    {
        HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk);
    }
    if ((status & HSUSBD_BUSINTSTS_VBUSDETIF_Msk) != 0u)
    {
        if (HSUSBD_IS_ATTACHED())
        {
            HSUSBD_ENABLE_USB();
        }
        else
        {
            HSUSBD_DISABLE_USB();
            if (g_port != 0)
            {
                g_port->in_busy = 0u;
                if (g_port->application != 0)
                {
                    Nulink2MeApplication_OnUsbDisconnected(
                        g_port->application);
                }
            }
        }
        HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_VBUSDETIF_Msk);
    }
}

static void ProcessControlEndpoint(void)
{
    uint32_t status;
    status = HSUSBD->CEPINTSTS & HSUSBD->CEPINTEN;
    if ((status & HSUSBD_CEPINTSTS_SETUPTKIF_Msk) != 0u)
    {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPTKIF_Msk);
        return;
    }
    if ((status & HSUSBD_CEPINTSTS_SETUPPKIF_Msk) != 0u)
    {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPPKIF_Msk);
        HSUSBD_ProcessSetupPacket();
        return;
    }
    if ((status & HSUSBD_CEPINTSTS_OUTTKIF_Msk) != 0u)
    {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_OUTTKIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
        return;
    }
    if ((status & HSUSBD_CEPINTSTS_INTKIF_Msk) != 0u)
    {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
        if ((status & HSUSBD_CEPINTSTS_STSDONEIF_Msk) == 0u)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk);
            HSUSBD_CtrlIn();
        }
        return;
    }
    if ((status & HSUSBD_CEPINTSTS_TXPKIF_Msk) != 0u)
    {
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
        if (g_hsusbd_CtrlInSize != 0u)
        {
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
        }
        else
        {
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk |
                                  HSUSBD_CEPINTEN_STSDONEIEN_Msk);
        }
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
        return;
    }
    if ((status & HSUSBD_CEPINTSTS_RXPKIF_Msk) != 0u)
    {
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_RXPKIF_Msk);
        HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk |
                              HSUSBD_CEPINTEN_STSDONEIEN_Msk);
        return;
    }
    if ((status & HSUSBD_CEPINTSTS_STSDONEIF_Msk) != 0u)
    {
        HSUSBD_UpdateDeviceState();
        HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
        HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
        return;
    }
    HSUSBD_CLR_CEP_INT_FLAG(status);
}

static void ProcessOutEndpoint(void)
{
    uint8_t report[NULINK2ME_HID_REPORT_SIZE];
    uint32_t status;
    uint32_t length;
    uint32_t index;
    status = HSUSBD->EP[EPB].EPINTSTS & HSUSBD->EP[EPB].EPINTEN;
    if ((status & (HSUSBD_EPINTSTS_RXPKIF_Msk |
                   HSUSBD_EPINTSTS_BUFFULLIF_Msk)) != 0u)
    {
        length = HSUSBD->EP[EPB].EPDATCNT & 0xFFFFu;
        if (length > NULINK2ME_HID_REPORT_SIZE)
        {
            length = NULINK2ME_HID_REPORT_SIZE;
        }
        for (index = 0u; index < length; ++index)
        {
            report[index] = HSUSBD->EP[EPB].EPDAT_BYTE;
        }
        if (g_port != 0 && g_port->application != 0)
        {
            (void)Nulink2MeApplication_OnUsbOut(g_port->application,
                                                report, length);
        }
    }
    HSUSBD_CLR_EP_INT_FLAG(EPB, status);
}

void USBD20_IRQHandler(void)
{
    uint32_t global_status;
    uint32_t status;
    global_status = HSUSBD->GINTSTS & HSUSBD->GINTEN;
    if (global_status == 0u)
    {
        return;
    }
    if ((global_status & HSUSBD_GINTSTS_USBIF_Msk) != 0u)
    {
        status = HSUSBD->BUSINTSTS & HSUSBD->BUSINTEN;
        ProcessBusInterrupt(status);
    }
    if ((global_status & HSUSBD_GINTSTS_CEPIF_Msk) != 0u)
    {
        ProcessControlEndpoint();
    }
    if ((global_status & HSUSBD_GINTSTS_EPAIF_Msk) != 0u)
    {
        status = HSUSBD->EP[EPA].EPINTSTS & HSUSBD->EP[EPA].EPINTEN;
        if ((status & HSUSBD_EPINTSTS_TXPKIF_Msk) != 0u && g_port != 0)
        {
            g_port->in_busy = 0u;
        }
        HSUSBD_CLR_EP_INT_FLAG(EPA, status);
    }
    if ((global_status & HSUSBD_GINTSTS_EPBIF_Msk) != 0u)
    {
        ProcessOutEndpoint();
    }
}
