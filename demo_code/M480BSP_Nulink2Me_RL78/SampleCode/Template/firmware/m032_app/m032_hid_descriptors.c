/* SPDX-License-Identifier: Apache-2.0 */
/* Descriptor layout is adapted from the Nuvoton M031 USB HID BSP sample. */
#include "m032_numicro_hid_port.h"

#define USB_VID 0x0416u
#define USB_PID 0x5020u
#define USB_CONFIG_LENGTH (LEN_CONFIG + LEN_INTERFACE + LEN_HID + (LEN_ENDPOINT * 2u))

static uint8_t g_report_descriptor[] =
{
    0x06u, 0x00u, 0xFFu,
    0x09u, 0x01u,
    0xA1u, 0x01u,
    0x15u, 0x00u,
    0x26u, 0xFFu, 0x00u,
    0x75u, 0x08u,
    0x95u, 0x40u,
    0x09u, 0x01u,
    0x81u, 0x02u,
    0x95u, 0x40u,
    0x09u, 0x01u,
    0x91u, 0x02u,
    0xC0u
};

static uint8_t g_device_descriptor[] =
{
    LEN_DEVICE, DESC_DEVICE, 0x10u, 0x01u, 0x00u, 0x00u, 0x00u,
    M032_HID_EP0_MAX_PACKET,
    (uint8_t)USB_VID, (uint8_t)(USB_VID >> 8),
    (uint8_t)USB_PID, (uint8_t)(USB_PID >> 8),
    0x00u, 0x01u, 0x01u, 0x02u, 0x00u, 0x01u
};

static uint8_t g_config_descriptor[] =
{
    LEN_CONFIG, DESC_CONFIG,
    (uint8_t)USB_CONFIG_LENGTH, (uint8_t)(USB_CONFIG_LENGTH >> 8),
    0x01u, 0x01u, 0x00u, 0x80u, 50u,
    LEN_INTERFACE, DESC_INTERFACE, 0x00u, 0x00u, 0x02u,
    0x03u, 0x00u, 0x00u, 0x00u,
    LEN_HID, DESC_HID, 0x11u, 0x01u, 0x00u, 0x01u, DESC_HID_RPT,
    (uint8_t)sizeof(g_report_descriptor),
    (uint8_t)(sizeof(g_report_descriptor) >> 8),
    LEN_ENDPOINT, DESC_ENDPOINT,
    (M032_HID_INT_IN_NUMBER | EP_INPUT), EP_INT,
    M032_HID_REPORT_SIZE, 0x00u, 0x01u,
    LEN_ENDPOINT, DESC_ENDPOINT,
    (M032_HID_INT_OUT_NUMBER | EP_OUTPUT), EP_INT,
    M032_HID_REPORT_SIZE, 0x00u, 0x01u
};

static uint8_t g_language_string[] = {4u, DESC_STRING, 0x09u, 0x04u};
static uint8_t g_vendor_string[] =
{
    16u, DESC_STRING, 'N', 0u, 'u', 0u, 'v', 0u,
    'o', 0u, 't', 0u, 'o', 0u, 'n', 0u
};
static uint8_t g_product_string[] =
{
    42u, DESC_STRING,
    'M', 0u, '0', 0u, '3', 0u, '2', 0u, ' ', 0u,
    'R', 0u, 'L', 0u, '7', 0u, '8', 0u, ' ', 0u,
    'P', 0u, 'r', 0u, 'o', 0u, 'g', 0u, 'r', 0u,
    'a', 0u, 'm', 0u, 'm', 0u, 'e', 0u, 'r', 0u
};
static uint8_t *g_strings[] =
{
    g_language_string, g_vendor_string, g_product_string, 0
};
static uint8_t *g_hid_reports[] = {g_report_descriptor, 0, 0};
static uint32_t g_hid_report_lengths[] = {sizeof(g_report_descriptor), 0u, 0u};
static uint32_t g_hid_descriptor_indexes[] =
{
    LEN_CONFIG + LEN_INTERFACE, 0u, 0u
};

const S_USBD_INFO_T g_M032HidDeviceInfo =
{
    g_device_descriptor,
    g_config_descriptor,
    g_strings,
    g_hid_reports,
    0,
    g_hid_report_lengths,
    g_hid_descriptor_indexes
};
