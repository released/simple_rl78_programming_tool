#include "serial_plp_framer.h"

#include <algorithm>

#include "rl78_programmer/plp_frame.h"

namespace rl78_programmer::sdk::detail {

void SerialPlpFramer::Reset() noexcept {
    bytes_.clear();
}

void SerialPlpFramer::Push(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length == 0u) {
        return;
    }
    bytes_.insert(bytes_.end(), data, data + length);
    if (bytes_.size() > PLP_REPORT_SIZE * 8u) {
        bytes_.erase(bytes_.begin(),
                     bytes_.end() -
                         static_cast<std::ptrdiff_t>(PLP_REPORT_SIZE * 4u));
    }
}

bool SerialPlpFramer::TryTake(
    std::array<std::uint8_t, PLP_REPORT_SIZE>* report) {
    RppPlpFrame frame{};
    if (report == nullptr) {
        return false;
    }
    for (;;) {
        const auto magic = std::find(bytes_.begin(), bytes_.end(), PLP_MAGIC);
        if (magic == bytes_.end()) {
            bytes_.clear();
            return false;
        }
        if (magic != bytes_.begin()) {
            bytes_.erase(bytes_.begin(), magic);
        }
        if (bytes_.size() < PLP_REPORT_SIZE) {
            return false;
        }
        if (Rpp_PlpDecode(bytes_.data(), &frame) == PLP_OK) {
            std::copy_n(bytes_.begin(), PLP_REPORT_SIZE, report->begin());
            bytes_.erase(
                bytes_.begin(),
                bytes_.begin() +
                    static_cast<std::ptrdiff_t>(PLP_REPORT_SIZE));
            return true;
        }
        bytes_.erase(bytes_.begin());
    }
}

std::size_t SerialPlpFramer::BufferedByteCount() const noexcept {
    return bytes_.size();
}

} // namespace rl78_programmer::sdk::detail
