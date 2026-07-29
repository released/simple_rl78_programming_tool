#pragma once

#include <string>
#include <utility>

namespace rl78_programmer::app {

class ValidatedImageSource final {
public:
    void Bind(std::wstring source_path) {
        source_path_ = std::move(source_path);
    }

    void Clear() noexcept {
        source_path_.clear();
    }

    [[nodiscard]] bool Matches(const std::wstring& source_path) const noexcept {
        return !source_path_.empty() && source_path_ == source_path;
    }

private:
    std::wstring source_path_;
};

} // namespace rl78_programmer::app
