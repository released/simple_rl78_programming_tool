#pragma once

#include <windows.h>

#include <filesystem>
#include <mutex>
#include <string>

namespace rl78_programmer::app {

class SessionFileLogger final {
public:
    SessionFileLogger() = default;
    ~SessionFileLogger();

    SessionFileLogger(const SessionFileLogger&) = delete;
    SessionFileLogger& operator=(const SessionFileLogger&) = delete;

    bool Open(const std::filesystem::path& directory,
              const std::wstring& base_name,
              std::wstring* error = nullptr);

    // Appends one UTF-8 record and supplies the CRLF terminator.
    bool AppendLine(const std::wstring& line,
                    std::wstring* error = nullptr);

    void Close();
    [[nodiscard]] bool IsOpen() const;

    // Returns the last successfully opened path, including after Close().
    [[nodiscard]] std::filesystem::path Path() const;

private:
    void CloseUnlocked();

    mutable std::mutex mutex_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::filesystem::path path_;
};

} // namespace rl78_programmer::app
