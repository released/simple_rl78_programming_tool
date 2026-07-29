#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rl78_programmer/plp_contract.h"

namespace rl78_programmer::sdk::detail {

class SerialPlpFramer {
public:
    void Reset() noexcept;
    void Push(const std::uint8_t* data, std::size_t length);
    [[nodiscard]] bool TryTake(
        std::array<std::uint8_t, PLP_REPORT_SIZE>* report);
    [[nodiscard]] std::size_t BufferedByteCount() const noexcept;

private:
    std::vector<std::uint8_t> bytes_;
};

} // namespace rl78_programmer::sdk::detail
