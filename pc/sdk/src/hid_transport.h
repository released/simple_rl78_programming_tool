#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "programmer_sdk.h"
#include "programmer_transport.h"
#include "rl78_programmer/plp_contract.h"

namespace rl78_programmer::sdk::detail {

class HidTransport final : public ProgrammerTransport {
public:
    HidTransport() = default;
    ~HidTransport() override;

    HidTransport(const HidTransport&) = delete;
    HidTransport& operator=(const HidTransport&) = delete;

    static std::vector<DeviceDescriptor> Enumerate(std::uint16_t vendor_id,
                                                    std::uint16_t product_id,
                                                    bool filter_ids);
    void Open(const DeviceDescriptor& descriptor, int timeout_ms);
    void Close() noexcept override;
    [[nodiscard]] bool IsOpen() const noexcept override;
    [[nodiscard]] DeviceDescriptor CurrentDevice() const override {
        return device_;
    }
    void Write(const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) override;
    std::array<std::uint8_t, PLP_REPORT_SIZE> Read(int timeout_ms) override;
    void WriteRaw(const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) override;
    std::array<std::uint8_t, PLP_REPORT_SIZE> ReadRaw(int timeout_ms) override;

private:
    void* handle_ = nullptr;
    DeviceDescriptor device_{};
    int timeout_ms_ = 1000;
};

} // namespace rl78_programmer::sdk::detail
