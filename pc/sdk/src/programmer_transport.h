#pragma once

#include <array>
#include <cstdint>

#include "programmer_sdk.h"
#include "rl78_programmer/plp_contract.h"

namespace rl78_programmer::sdk::detail {

class ProgrammerTransport {
public:
    virtual ~ProgrammerTransport() = default;

    ProgrammerTransport(const ProgrammerTransport&) = delete;
    ProgrammerTransport& operator=(const ProgrammerTransport&) = delete;

    virtual void Close() noexcept = 0;
    [[nodiscard]] virtual bool IsOpen() const noexcept = 0;
    [[nodiscard]] virtual DeviceDescriptor CurrentDevice() const = 0;
    virtual void Write(
        const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) = 0;
    virtual std::array<std::uint8_t, PLP_REPORT_SIZE> Read(
        int timeout_ms) = 0;
    virtual void WriteRaw(
        const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) = 0;
    virtual std::array<std::uint8_t, PLP_REPORT_SIZE> ReadRaw(
        int timeout_ms) = 0;

protected:
    ProgrammerTransport() = default;
};

} // namespace rl78_programmer::sdk::detail
