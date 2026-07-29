#pragma once

#include <cstdint>
#include <string>

struct AppConfig {
    std::uint16_t vendor_id = 0x0416u;
    std::uint16_t product_id = 0x5020u;
    int timeout_ms = 1000;
    std::wstring last_mot_path;
    std::wstring last_oip_path;
    std::wstring last_bridge_firmware_path;
    int board_profile = 0;
    int bridge_firmware_profile = 0;
    bool save_log_to_file = false;
};

std::wstring AppConfigPath();
bool LoadAppConfig(AppConfig* config, std::wstring* error = nullptr);
bool SaveAppConfig(const AppConfig& config, std::wstring* error = nullptr);
