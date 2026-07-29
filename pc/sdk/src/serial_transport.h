#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "programmer_transport.h"
#include "serial_plp_framer.h"

namespace rl78_programmer::sdk::detail {

class SerialTransport final : public ProgrammerTransport {
public:
    SerialTransport() = default;
    ~SerialTransport() override;

    static std::vector<DeviceDescriptor> Enumerate();

    void Open(const DeviceDescriptor& descriptor, int timeout_ms);
    void Close() noexcept override;
    [[nodiscard]] bool IsOpen() const noexcept override;
    [[nodiscard]] DeviceDescriptor CurrentDevice() const override {
        return device_;
    }
    void Write(
        const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) override;
    std::array<std::uint8_t, PLP_REPORT_SIZE> Read(
        int timeout_ms) override;
    void WriteRaw(
        const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) override;
    std::array<std::uint8_t, PLP_REPORT_SIZE> ReadRaw(
        int timeout_ms) override;

private:
    void* handle_ = nullptr;
    DeviceDescriptor device_{};
    SerialPlpFramer framer_;
    int timeout_ms_ = 1000;
};

} // namespace rl78_programmer::sdk::detail
