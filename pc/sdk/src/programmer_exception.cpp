#include "programmer_sdk.h"

#include <string>

namespace rl78_programmer::sdk {

ProgrammerException::ProgrammerException(std::uint32_t status,
                                         const std::string& message)
    : std::runtime_error(message + " [" + std::to_string(status) + "]"),
      status_(status) {}

} // namespace rl78_programmer::sdk
