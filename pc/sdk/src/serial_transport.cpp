#include "serial_transport.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <string>

namespace rl78_programmer::sdk::detail {
namespace {

HANDLE AsHandle(void* value) {
    return reinterpret_cast<HANDLE>(value);
}

std::string Win32Error(const char* operation, DWORD error) {
    std::ostringstream out;
    out << operation << " failed, win32=" << error;
    return out.str();
}

std::uint32_t MapIoError(DWORD error) {
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return PLP_ERR_UART_NOT_FOUND;
    }
    if (error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_INVALID_HANDLE ||
        error == ERROR_OPERATION_ABORTED) {
        return PLP_ERR_UART_REMOVED;
    }
    return PLP_ERR_UART_ACCESS;
}

std::wstring NormalizePortPath(const std::wstring& path) {
    if (path.rfind(L"\\\\.\\", 0u) == 0u) {
        return path;
    }
    return L"\\\\.\\" + path;
}

DWORD Transfer(HANDLE handle, bool write, std::uint8_t* data, DWORD size,
               int timeout_ms) {
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (operation.hEvent == nullptr) {
        const DWORD error = GetLastError();
        throw ProgrammerException(MapIoError(error),
                                  Win32Error("CreateEvent(UART)", error));
    }
    DWORD transferred = 0;
    BOOL started = FALSE;
    if (write) {
        started = WriteFile(handle, data, size, nullptr, &operation);
    } else {
        started = ReadFile(handle, data, size, nullptr, &operation);
    }
    if (!started && GetLastError() != ERROR_IO_PENDING) {
        const DWORD error = GetLastError();
        CloseHandle(operation.hEvent);
        throw ProgrammerException(
            MapIoError(error),
            Win32Error(write ? "WriteFile(UART)" : "ReadFile(UART)", error));
    }
    const DWORD wait = WaitForSingleObject(
        operation.hEvent, static_cast<DWORD>((std::max)(timeout_ms, 1)));
    if (wait != WAIT_OBJECT_0) {
        CancelIoEx(handle, &operation);
        WaitForSingleObject(operation.hEvent, INFINITE);
        CloseHandle(operation.hEvent);
        if (wait == WAIT_TIMEOUT) {
            throw ProgrammerException(
                PLP_ERR_UART_TIMEOUT,
                write ? "UART write timeout" : "UART read timeout");
        }
        throw ProgrammerException(PLP_ERR_UART_ACCESS,
                                  "UART overlapped wait failed");
    }
    if (!GetOverlappedResult(handle, &operation, &transferred, FALSE)) {
        const DWORD error = GetLastError();
        CloseHandle(operation.hEvent);
        throw ProgrammerException(
            MapIoError(error),
            Win32Error(write ? "GetOverlappedResult(UART write)"
                             : "GetOverlappedResult(UART read)",
                       error));
    }
    CloseHandle(operation.hEvent);
    return transferred;
}

} // namespace

SerialTransport::~SerialTransport() {
    Close();
}

std::vector<DeviceDescriptor> SerialTransport::Enumerate() {
    std::vector<DeviceDescriptor> result;
    wchar_t target[512] = {};
    for (unsigned number = 1u; number <= 256u; ++number) {
        const std::wstring name = L"COM" + std::to_wstring(number);
        if (QueryDosDeviceW(name.c_str(), target,
                            static_cast<DWORD>(std::size(target))) == 0u) {
            continue;
        }
        DeviceDescriptor descriptor;
        descriptor.path = NormalizePortPath(name);
        descriptor.manufacturer = L"External VCOM";
        descriptor.product = L"M032 TEST BOARD";
        descriptor.serial_number = name;
        descriptor.transport = TransportKind::Uart;
        descriptor.baud_rate = 115200u;
        result.push_back(std::move(descriptor));
    }
    return result;
}

void SerialTransport::Open(const DeviceDescriptor& descriptor,
                           int timeout_ms) {
    Close();
    const std::wstring path = NormalizePortPath(descriptor.path);
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                0u, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        throw ProgrammerException(MapIoError(error),
                                  Win32Error("CreateFile(UART)", error));
    }

    DCB settings{};
    settings.DCBlength = sizeof(settings);
    if (!GetCommState(handle, &settings)) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        throw ProgrammerException(MapIoError(error),
                                  Win32Error("GetCommState", error));
    }
    settings.BaudRate =
        descriptor.baud_rate == 0u ? 115200u : descriptor.baud_rate;
    settings.ByteSize = 8u;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fParity = FALSE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;
    settings.fDtrControl = DTR_CONTROL_DISABLE;
    settings.fDsrSensitivity = FALSE;
    settings.fTXContinueOnXoff = TRUE;
    settings.fOutX = FALSE;
    settings.fInX = FALSE;
    settings.fErrorChar = FALSE;
    settings.fNull = FALSE;
    settings.fRtsControl = RTS_CONTROL_DISABLE;
    settings.fAbortOnError = FALSE;
    COMMTIMEOUTS timeouts{};
    /*
     * Return buffered bytes as soon as at least one byte is available.
     * PLP and NuvoISP both use 64-byte frames; waiting for the entire
     * 256-byte host scratch buffer would discard a valid response when the
     * outer overlapped timeout cancels the still-pending read.
     */
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 50u;
    if (!SetCommState(handle, &settings) ||
        !SetCommTimeouts(handle, &timeouts) ||
        !SetupComm(handle, 4096u, 4096u) ||
        !PurgeComm(handle, PURGE_RXABORT | PURGE_RXCLEAR |
                               PURGE_TXABORT | PURGE_TXCLEAR)) {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        throw ProgrammerException(MapIoError(error),
                                  Win32Error("Configure UART", error));
    }

    handle_ = handle;
    device_ = descriptor;
    device_.path = path;
    device_.transport = TransportKind::Uart;
    device_.baud_rate = settings.BaudRate;
    timeout_ms_ = (std::max)(timeout_ms, 100);
    framer_.Reset();
}

void SerialTransport::Close() noexcept {
    HANDLE handle = AsHandle(handle_);
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CancelIoEx(handle, nullptr);
        CloseHandle(handle);
    }
    handle_ = nullptr;
    device_ = {};
    framer_.Reset();
}

bool SerialTransport::IsOpen() const noexcept {
    const HANDLE handle = AsHandle(handle_);
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

void SerialTransport::Write(
    const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) {
    WriteRaw(report);
}

void SerialTransport::WriteRaw(
    const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) {
    if (!IsOpen()) {
        throw ProgrammerException(PLP_ERR_UART_REMOVED,
                                  "UART port is not open");
    }
    std::array<std::uint8_t, PLP_REPORT_SIZE> wire = report;
    const DWORD written = Transfer(
        AsHandle(handle_), true, wire.data(),
        static_cast<DWORD>(wire.size()), timeout_ms_);
    if (written != wire.size()) {
        throw ProgrammerException(PLP_ERR_UART_ACCESS,
                                  "UART write was shorter than 64 bytes");
    }
    /*
     * An overlapped write may complete after the bytes are accepted by the
     * driver but before the external VCOM has shifted the final stop bit.
     * Run-application and reset commands intentionally have no response and
     * the caller closes the COM handle immediately, so drain the transmitter
     * here to keep Close() from cancelling the last command in the queue.
     */
    if (!FlushFileBuffers(AsHandle(handle_))) {
        const DWORD error = GetLastError();
        throw ProgrammerException(MapIoError(error),
                                  Win32Error("FlushFileBuffers(UART)",
                                             error));
    }
}

std::array<std::uint8_t, PLP_REPORT_SIZE> SerialTransport::ReadRaw(
    int timeout_ms) {
    if (!IsOpen()) {
        throw ProgrammerException(PLP_ERR_UART_REMOVED,
                                  "UART port is not open");
    }
    std::array<std::uint8_t, PLP_REPORT_SIZE> report{};
    const ULONGLONG started = GetTickCount64();
    const int bounded_timeout =
        (std::max)(timeout_ms > 0 ? timeout_ms : timeout_ms_, 1);
    std::size_t offset = 0u;
    while (offset < report.size()) {
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= static_cast<ULONGLONG>(bounded_timeout)) {
            throw ProgrammerException(PLP_ERR_UART_TIMEOUT,
                                      "UART raw packet timeout");
        }
        const int remaining = static_cast<int>(
            static_cast<ULONGLONG>(bounded_timeout) - elapsed);
        const DWORD received = Transfer(
            AsHandle(handle_), false, report.data() + offset,
            static_cast<DWORD>(report.size() - offset), remaining);
        if (received == 0u) {
            continue;
        }
        offset += received;
    }
    return report;
}

std::array<std::uint8_t, PLP_REPORT_SIZE> SerialTransport::Read(
    int timeout_ms) {
    if (!IsOpen()) {
        throw ProgrammerException(PLP_ERR_UART_REMOVED,
                                  "UART port is not open");
    }
    std::array<std::uint8_t, PLP_REPORT_SIZE> report{};
    if (framer_.TryTake(&report)) {
        return report;
    }
    const ULONGLONG started = GetTickCount64();
    const int bounded_timeout =
        (std::max)(timeout_ms > 0 ? timeout_ms : timeout_ms_, 1);
    for (;;) {
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= static_cast<ULONGLONG>(bounded_timeout)) {
            throw ProgrammerException(PLP_ERR_UART_TIMEOUT,
                                      "PLP response timeout on UART");
        }
        const int remaining = static_cast<int>(
            static_cast<ULONGLONG>(bounded_timeout) - elapsed);
        std::array<std::uint8_t, 256> bytes{};
        const DWORD received = Transfer(
            AsHandle(handle_), false, bytes.data(),
            static_cast<DWORD>(bytes.size()), remaining);
        if (received == 0u) {
            continue;
        }
        framer_.Push(bytes.data(), received);
        if (framer_.TryTake(&report)) {
            return report;
        }
    }
}

} // namespace rl78_programmer::sdk::detail
