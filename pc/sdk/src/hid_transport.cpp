#include "hid_transport.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <tuple>
#include <type_traits>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace rl78_programmer::sdk::detail {
namespace {

HANDLE AsHandle(void* value) {
    return reinterpret_cast<HANDLE>(value);
}

struct DevInfoSetDeleter {
    void operator()(HDEVINFO value) const noexcept {
        if (value != INVALID_HANDLE_VALUE) {
            SetupDiDestroyDeviceInfoList(value);
        }
    }
};

using DevInfoSet = std::unique_ptr<std::remove_pointer<HDEVINFO>::type, DevInfoSetDeleter>;

std::string Win32Error(const char* operation) {
    std::ostringstream out;
    out << operation << " failed, win32=" << GetLastError();
    return out.str();
}

std::wstring QueryWideString(HANDLE device,
                             BOOLEAN(__stdcall* query)(HANDLE, PVOID, ULONG)) {
    wchar_t buffer[256] = {};
    return query(device, buffer, static_cast<ULONG>(sizeof(buffer))) ? buffer : L"";
}

void PopulateDescriptor(HANDLE device, DeviceDescriptor* descriptor) {
    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (HidD_GetAttributes(device, &attributes)) {
        descriptor->vendor_id = attributes.VendorID;
        descriptor->product_id = attributes.ProductID;
    }
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (HidD_GetPreparsedData(device, &preparsed) && preparsed != nullptr) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            descriptor->usage_page = caps.UsagePage;
            descriptor->usage = caps.Usage;
            descriptor->input_report_length = caps.InputReportByteLength;
            descriptor->output_report_length = caps.OutputReportByteLength;
        }
        HidD_FreePreparsedData(preparsed);
    }
    descriptor->manufacturer = QueryWideString(device, HidD_GetManufacturerString);
    descriptor->product = QueryWideString(device, HidD_GetProductString);
    descriptor->serial_number = QueryWideString(device, HidD_GetSerialNumberString);
}

DWORD WriteOverlapped(HANDLE handle, const std::uint8_t* data, DWORD size, int timeout_ms) {
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (operation.hEvent == nullptr) {
        throw ProgrammerException(PLP_ERR_USB_ACCESS, Win32Error("CreateEvent(write)"));
    }
    DWORD written = 0;
    const BOOL started = WriteFile(handle, data, size, nullptr, &operation);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(operation.hEvent);
        throw ProgrammerException(PLP_ERR_USB_ACCESS, Win32Error("WriteFile"));
    }
    const DWORD wait = WaitForSingleObject(operation.hEvent,
                                           static_cast<DWORD>((std::max)(timeout_ms, 1)));
    if (wait != WAIT_OBJECT_0) {
        CancelIoEx(handle, &operation);
        CloseHandle(operation.hEvent);
        throw ProgrammerException(wait == WAIT_TIMEOUT ? PLP_ERR_USB_TIMEOUT : PLP_ERR_USB_ACCESS,
                                  wait == WAIT_TIMEOUT ? "HID write timeout" : "HID write wait failed");
    }
    if (!GetOverlappedResult(handle, &operation, &written, FALSE)) {
        const auto message = Win32Error("GetOverlappedResult(write)");
        CloseHandle(operation.hEvent);
        throw ProgrammerException(PLP_ERR_USB_ACCESS, message);
    }
    CloseHandle(operation.hEvent);
    return written;
}

std::vector<std::uint8_t> ReadOverlapped(HANDLE handle, DWORD size, int timeout_ms) {
    std::vector<std::uint8_t> data(size, std::uint8_t{0});
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (operation.hEvent == nullptr) {
        throw ProgrammerException(PLP_ERR_USB_ACCESS, Win32Error("CreateEvent(read)"));
    }
    DWORD received = 0;
    const BOOL started = ReadFile(handle, data.data(), size, nullptr, &operation);
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        CloseHandle(operation.hEvent);
        throw ProgrammerException(PLP_ERR_USB_ACCESS, Win32Error("ReadFile"));
    }
    const DWORD wait = WaitForSingleObject(operation.hEvent,
                                           static_cast<DWORD>((std::max)(timeout_ms, 1)));
    if (wait != WAIT_OBJECT_0) {
        CancelIoEx(handle, &operation);
        CloseHandle(operation.hEvent);
        throw ProgrammerException(wait == WAIT_TIMEOUT ? PLP_ERR_USB_TIMEOUT : PLP_ERR_USB_ACCESS,
                                  wait == WAIT_TIMEOUT ? "HID read timeout" : "HID read wait failed");
    }
    if (!GetOverlappedResult(handle, &operation, &received, FALSE)) {
        const auto message = Win32Error("GetOverlappedResult(read)");
        CloseHandle(operation.hEvent);
        throw ProgrammerException(PLP_ERR_USB_ACCESS, message);
    }
    CloseHandle(operation.hEvent);
    data.resize(received);
    return data;
}

} // namespace

HidTransport::~HidTransport() {
    Close();
}

std::vector<DeviceDescriptor> HidTransport::Enumerate(std::uint16_t vendor_id,
                                                      std::uint16_t product_id,
                                                      bool filter_ids) {
    std::vector<DeviceDescriptor> result;
    GUID hid_guid{};
    HidD_GetHidGuid(&hid_guid);
    DevInfoSet devices(SetupDiGetClassDevsW(&hid_guid, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (devices.get() == INVALID_HANDLE_VALUE) {
        return result;
    }
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &hid_guid, index, &interface_data)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) {
                break;
            }
            continue;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.get(), &interface_data, nullptr, 0, &required, nullptr);
        if (required == 0) {
            continue;
        }
        std::vector<std::uint8_t> buffer(required, std::uint8_t{0});
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.get(), &interface_data, detail,
                                              required, nullptr, nullptr)) {
            continue;
        }
        HANDLE device = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (device == INVALID_HANDLE_VALUE) {
            continue;
        }
        DeviceDescriptor descriptor;
        descriptor.path = detail->DevicePath;
        PopulateDescriptor(device, &descriptor);
        CloseHandle(device);
        if (filter_ids &&
            (descriptor.vendor_id != vendor_id || descriptor.product_id != product_id)) {
            continue;
        }
        result.push_back(std::move(descriptor));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return std::tie(left.vendor_id, left.product_id, left.usage_page, left.usage,
                        left.serial_number, left.path) <
               std::tie(right.vendor_id, right.product_id, right.usage_page, right.usage,
                        right.serial_number, right.path);
    });
    return result;
}

void HidTransport::Open(const DeviceDescriptor& descriptor, int timeout_ms) {
    Close();
    HANDLE handle = CreateFileW(descriptor.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw ProgrammerException(PLP_ERR_USB_ACCESS, Win32Error("CreateFile(HID)"));
    }
    handle_ = handle;
    device_ = descriptor;
    timeout_ms_ = (std::max)(timeout_ms, 100);
    HidD_SetNumInputBuffers(handle, 64);
    PopulateDescriptor(handle, &device_);
}

void HidTransport::Close() noexcept {
    HANDLE handle = AsHandle(handle_);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(handle, nullptr);
        CloseHandle(handle);
    }
    handle_ = nullptr;
    device_ = {};
}

bool HidTransport::IsOpen() const noexcept {
    const HANDLE handle = AsHandle(handle_);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

void HidTransport::Write(const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) {
    WriteRaw(report);
}

void HidTransport::WriteRaw(const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) {
    if (!IsOpen()) {
        throw ProgrammerException(PLP_ERR_USB_REMOVED, "HID device is not open");
    }
    const std::uint16_t report_length = device_.output_report_length == 0 ? 65 : device_.output_report_length;
    if (report_length < PLP_REPORT_SIZE) {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "HID output report is smaller than PLP report");
    }
    std::vector<std::uint8_t> wire(report_length, std::uint8_t{0});
    if (report_length == PLP_REPORT_SIZE) {
        std::copy(report.begin(), report.end(), wire.begin());
    } else {
        std::copy(report.begin(), report.end(), wire.begin() + 1);
    }
    if (WriteOverlapped(AsHandle(handle_), wire.data(), static_cast<DWORD>(wire.size()), timeout_ms_) == 0) {
        throw ProgrammerException(PLP_ERR_USB_ACCESS, "HID write completed with zero bytes");
    }
}

std::array<std::uint8_t, PLP_REPORT_SIZE> HidTransport::Read(int timeout_ms) {
    const auto report = ReadRaw(timeout_ms);
    if (report[0] != PLP_MAGIC) {
        throw ProgrammerException(PLP_ERR_BAD_MAGIC, "HID input does not contain a PLP report");
    }
    return report;
}

std::array<std::uint8_t, PLP_REPORT_SIZE> HidTransport::ReadRaw(int timeout_ms) {
    if (!IsOpen()) {
        throw ProgrammerException(PLP_ERR_USB_REMOVED, "HID device is not open");
    }
    const std::uint16_t report_length = device_.input_report_length == 0 ? 65 : device_.input_report_length;
    const auto wire = ReadOverlapped(AsHandle(handle_), report_length,
                                     timeout_ms > 0 ? timeout_ms : timeout_ms_);
    std::array<std::uint8_t, PLP_REPORT_SIZE> report{};
    if (wire.size() >= PLP_REPORT_SIZE + 1u && wire[0] == 0u) {
        std::copy_n(wire.begin() + 1, PLP_REPORT_SIZE, report.begin());
    } else if (wire.size() >= PLP_REPORT_SIZE) {
        std::copy_n(wire.begin(), PLP_REPORT_SIZE, report.begin());
    } else {
        throw ProgrammerException(PLP_ERR_BAD_LENGTH, "HID input report is shorter than 64 bytes");
    }
    return report;
}

} // namespace rl78_programmer::sdk::detail
