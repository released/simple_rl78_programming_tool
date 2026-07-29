#include "app_config.h"

#include <windows.h>

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>

namespace {

std::wstring ReadValue(const std::wstring& path,
                       const wchar_t* key,
                       const wchar_t* fallback) {
    wchar_t value[2048] = {};
    GetPrivateProfileStringW(L"Programmer", key, fallback, value,
                             static_cast<DWORD>(std::size(value)), path.c_str());
    return value;
}

std::uint16_t ParseHex16(const std::wstring& text, std::uint16_t fallback) {
    try {
        std::size_t used = 0;
        const auto value = std::stoul(text, &used, 0);
        return used == text.size() && value <= 0xFFFFu ? static_cast<std::uint16_t>(value) : fallback;
    } catch (...) {
        return fallback;
    }
}

bool ParseBool01(const std::wstring& text, bool fallback) {
    if (text == L"0") {
        return false;
    }
    if (text == L"1") {
        return true;
    }
    return fallback;
}

bool WriteValue(const std::wstring& path, const wchar_t* key, const std::wstring& value) {
    return WritePrivateProfileStringW(L"Programmer", key, value.c_str(), path.c_str()) != FALSE;
}

} // namespace

std::wstring AppConfigPath() {
    wchar_t module[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, module, static_cast<DWORD>(std::size(module)));
    std::wstring path(module, length);
    const auto slash = path.find_last_of(L"\\/");
    path.resize(slash == std::wstring::npos ? 0 : slash + 1);
    path += L"rl78_programmer.ini";
    return path;
}

bool LoadAppConfig(AppConfig* config, std::wstring* error) {
    if (config == nullptr) {
        if (error) *error = L"Configuration destination is null.";
        return false;
    }
    const auto path = AppConfigPath();
    config->vendor_id = ParseHex16(ReadValue(path, L"VendorId", L"0x0416"), config->vendor_id);
    config->product_id = ParseHex16(ReadValue(path, L"ProductId", L"0x5020"), config->product_id);
    try {
        config->timeout_ms = (std::max)(100, std::stoi(ReadValue(path, L"TimeoutMs", L"1000")));
    } catch (...) {
        config->timeout_ms = 1000;
    }
    config->last_mot_path = ReadValue(path, L"LastMotPath", L"");
    config->last_oip_path = ReadValue(path, L"LastOipPath", L"");
    config->last_bridge_firmware_path = ReadValue(path, L"LastBridgeFirmwarePath", L"");
    try {
        config->board_profile =
            (std::max)(0, (std::min)(2, std::stoi(
                ReadValue(path, L"BoardProfile", L"0"))));
    } catch (...) {
        config->board_profile = 0;
    }
    try {
        config->bridge_firmware_profile =
            (std::max)(0, (std::min)(2, std::stoi(ReadValue(path, L"BridgeFirmwareProfile", L"0"))));
    } catch (...) {
        config->bridge_firmware_profile = 0;
    }
    config->save_log_to_file =
        ParseBool01(ReadValue(path, L"SaveLogToFile", L"0"), false);
    return true;
}

bool SaveAppConfig(const AppConfig& config, std::wstring* error) {
    const auto path = AppConfigPath();
    std::wostringstream vid;
    std::wostringstream pid;
    vid << L"0x" << std::uppercase << std::hex << std::setw(4) << std::setfill(L'0') << config.vendor_id;
    pid << L"0x" << std::uppercase << std::hex << std::setw(4) << std::setfill(L'0') << config.product_id;
    const bool ok = WriteValue(path, L"VendorId", vid.str()) &&
                    WriteValue(path, L"ProductId", pid.str()) &&
                    WriteValue(path, L"TimeoutMs", std::to_wstring(config.timeout_ms)) &&
                    WriteValue(path, L"LastMotPath", config.last_mot_path) &&
                    WriteValue(path, L"LastOipPath", config.last_oip_path) &&
                    WriteValue(path, L"LastBridgeFirmwarePath", config.last_bridge_firmware_path) &&
                    WriteValue(path, L"BoardProfile",
                               std::to_wstring(config.board_profile)) &&
                    WriteValue(path, L"BridgeFirmwareProfile",
                               std::to_wstring(config.bridge_firmware_profile)) &&
                    WriteValue(path, L"SaveLogToFile",
                               config.save_log_to_file ? L"1" : L"0");
    if (!ok && error) {
        *error = L"Failed to save configuration: " + path +
                 L" (win32=" + std::to_wstring(GetLastError()) + L")";
    }
    return ok;
}
