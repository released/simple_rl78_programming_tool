#include "session_file_logger.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

namespace rl78_programmer::app {
namespace {

constexpr std::uint8_t kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};
constexpr unsigned kMaximumCollisionSuffix = 999u;

void SetError(std::wstring* error, const std::wstring& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::wstring Win32ErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message;
    if (length != 0u && buffer != nullptr) {
        message.assign(buffer, static_cast<std::size_t>(length));
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    if (message.empty()) {
        message = L"Unknown Windows error";
    }
    return message + L" (win32=" + std::to_wstring(code) + L")";
}

std::filesystem::path CandidatePath(const std::filesystem::path& directory,
                                    const std::wstring& base_name,
                                    unsigned suffix) {
    std::wostringstream filename;
    filename << base_name;
    if (suffix != 0u) {
        filename << L'_' << std::setw(3) << std::setfill(L'0') << suffix;
    }
    filename << L".log";
    return directory / filename.str();
}

bool WriteAll(HANDLE handle,
              const std::uint8_t* data,
              std::size_t size,
              DWORD* error_code) {
    std::size_t offset = 0u;
    while (offset < size) {
        const std::size_t remaining = size - offset;
        const DWORD request = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        DWORD written = 0u;
        if (WriteFile(handle, data + offset, request, &written, nullptr) == FALSE) {
            if (error_code != nullptr) {
                *error_code = GetLastError();
            }
            return false;
        }
        if (written == 0u) {
            if (error_code != nullptr) {
                *error_code = ERROR_WRITE_FAULT;
            }
            return false;
        }
        offset += written;
    }
    return true;
}

bool WideToUtf8(const std::wstring& text,
                std::vector<std::uint8_t>* utf8,
                DWORD* error_code) {
    if (utf8 == nullptr) {
        if (error_code != nullptr) {
            *error_code = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    utf8->clear();
    if (text.empty()) {
        return true;
    }
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        if (error_code != nullptr) {
            *error_code = ERROR_INVALID_PARAMETER;
        }
        return false;
    }
    const int source_length = static_cast<int>(text.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), source_length,
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        if (error_code != nullptr) {
            *error_code = GetLastError();
        }
        return false;
    }
    utf8->resize(static_cast<std::size_t>(required));
    const int converted = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), source_length,
        reinterpret_cast<char*>(utf8->data()), required, nullptr, nullptr);
    if (converted != required) {
        if (error_code != nullptr) {
            *error_code = GetLastError();
        }
        utf8->clear();
        return false;
    }
    return true;
}

} // namespace

SessionFileLogger::~SessionFileLogger() {
    Close();
}

bool SessionFileLogger::Open(const std::filesystem::path& directory,
                             const std::wstring& base_name,
                             std::wstring* error) {
    std::lock_guard<std::mutex> guard(mutex_);
    CloseUnlocked();
    path_.clear();
    if (error != nullptr) {
        error->clear();
    }
    if (directory.empty()) {
        SetError(error, L"Session log directory is empty.");
        return false;
    }
    if (base_name.empty()) {
        SetError(error, L"Session log base name is empty.");
        return false;
    }
    const std::filesystem::path base_path(base_name);
    if (base_path.filename().wstring() != base_name ||
        base_name == L"." || base_name == L"..") {
        SetError(error, L"Session log base name is invalid: " + base_name);
        return false;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error) {
        SetError(error, L"Cannot create session log directory '" +
                        directory.wstring() + L"': " +
                        Win32ErrorText(static_cast<DWORD>(filesystem_error.value())));
        return false;
    }
    if (!std::filesystem::is_directory(directory, filesystem_error) ||
        filesystem_error) {
        const DWORD code = filesystem_error ?
            static_cast<DWORD>(filesystem_error.value()) : ERROR_DIRECTORY;
        SetError(error, L"Session log path is not a directory '" +
                        directory.wstring() + L"': " + Win32ErrorText(code));
        return false;
    }

    for (unsigned suffix = 0u; suffix <= kMaximumCollisionSuffix; ++suffix) {
        const auto candidate = CandidatePath(directory, base_name, suffix);
        const HANDLE handle = CreateFileW(
            candidate.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) {
                continue;
            }
            SetError(error, L"Cannot create session log file '" +
                            candidate.wstring() + L"': " + Win32ErrorText(code));
            return false;
        }

        DWORD code = ERROR_SUCCESS;
        if (!WriteAll(handle, kUtf8Bom, sizeof(kUtf8Bom), &code) ||
            FlushFileBuffers(handle) == FALSE) {
            if (code == ERROR_SUCCESS) {
                code = GetLastError();
            }
            CloseHandle(handle);
            DeleteFileW(candidate.c_str());
            SetError(error, L"Cannot initialize session log file '" +
                            candidate.wstring() + L"': " + Win32ErrorText(code));
            return false;
        }

        handle_ = handle;
        path_ = candidate;
        return true;
    }

    SetError(error, L"Cannot create a unique session log file in '" +
                    directory.wstring() + L"': all suffixes 000..999 are in use.");
    return false;
}

bool SessionFileLogger::AppendLine(const std::wstring& line,
                                   std::wstring* error) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (error != nullptr) {
        error->clear();
    }
    if (handle_ == INVALID_HANDLE_VALUE) {
        SetError(error, L"Session log is not open.");
        return false;
    }

    std::vector<std::uint8_t> utf8;
    DWORD code = ERROR_SUCCESS;
    if (!WideToUtf8(line, &utf8, &code)) {
        SetError(error, L"Cannot convert session log text to UTF-8 for '" +
                        path_.wstring() + L"': " + Win32ErrorText(code));
        return false;
    }
    utf8.push_back(static_cast<std::uint8_t>('\r'));
    utf8.push_back(static_cast<std::uint8_t>('\n'));

    if (!WriteAll(handle_, utf8.data(), utf8.size(), &code)) {
        const auto failed_path = path_;
        CloseUnlocked();
        SetError(error, L"Cannot write session log file '" +
                        failed_path.wstring() + L"': " + Win32ErrorText(code));
        return false;
    }
    if (FlushFileBuffers(handle_) == FALSE) {
        code = GetLastError();
        const auto failed_path = path_;
        CloseUnlocked();
        SetError(error, L"Cannot flush session log file '" +
                        failed_path.wstring() + L"': " + Win32ErrorText(code));
        return false;
    }
    return true;
}

void SessionFileLogger::Close() {
    std::lock_guard<std::mutex> guard(mutex_);
    CloseUnlocked();
}

bool SessionFileLogger::IsOpen() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return handle_ != INVALID_HANDLE_VALUE;
}

std::filesystem::path SessionFileLogger::Path() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return path_;
}

void SessionFileLogger::CloseUnlocked() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle_);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

} // namespace rl78_programmer::app
