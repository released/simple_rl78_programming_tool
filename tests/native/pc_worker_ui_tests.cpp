#include "main_frame.h"

#include <condition_variable>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {
CWinApp test_app;
unsigned checks = 0;
void Check(bool condition, const char* text) {
    if (!condition) throw std::runtime_error(text);
    ++checks;
    std::cout << "PASS: " << text << '\n';
}
void PumpUntil(const std::function<bool()>& complete) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!complete() && std::chrono::steady_clock::now() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message != WM_QUIT) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(complete(), "native timer completion stays bounded");
}
} // namespace

class CMainFrameContractTest {
public:
    static void Run() {
        auto* frame = new CMainFrame();
        Check(frame->Create(nullptr, L"Isolated worker contract fixture", WS_OVERLAPPEDWINDOW,
                            CRect(0, 0, 1280, 840)) != FALSE, "MFC controls initialize with synthetic INI");
        const HWND window = frame->GetSafeHwnd();
        test_app.m_pMainWnd = frame;
        frame->StartWorker(CMainFrame::WorkKind::ProgramOnline, [] {
            auto result = std::make_unique<CMainFrame::WorkerResult>();
            sdk_result(*result);
            return result;
        });
        PumpUntil([&] { return !frame->worker_.IsBusy(); });
        CString unknown_text;
        frame->program_status_.GetWindowTextW(unknown_text);
        Check(unknown_text.Find(L"OUTCOME_UNKNOWN") >= 0 && frame->history_.GetItemCount() == 1,
              "unknown outcome remains visible in status and history");
        std::mutex mutex;
        std::condition_variable condition;
        bool release = false;
        unsigned second_starts = 0;
        frame->StartWorker(CMainFrame::WorkKind::Scan, [&] {
            std::unique_lock<std::mutex> guard(mutex);
            condition.wait(guard, [&] { return release; });
            auto result = std::make_unique<CMainFrame::WorkerResult>();
            result->message = L"Synthetic SDK operation completed";
            return result;
        });
        frame->StartWorker(CMainFrame::WorkKind::Scan, [&] {
            ++second_starts;
            return std::make_unique<CMainFrame::WorkerResult>();
        });
        Check(frame->worker_.IsBusy() && second_starts == 0, "native double trigger owns one task");
        Check(!frame->online_button_.IsWindowEnabled() &&
              !frame->connect_button_.IsWindowEnabled(), "exclusive UI controls remain disabled while busy");
        const auto before = std::chrono::steady_clock::now();
        SendMessageW(window, WM_CLOSE, 0, 0);
        Check(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(100),
              "WM_CLOSE returns without joining a running worker");
        Check(IsWindow(window) && frame->cancel_requested_.load(),
              "closing view remains alive with cancellation requested");
        {
            std::lock_guard<std::mutex> guard(mutex);
            release = true;
        }
        condition.notify_one();
        PumpUntil([&] { return !IsWindow(window); });
        test_app.m_pMainWnd = nullptr;
        Check(second_starts == 0, "late completion is consumed once before frame destruction");

        frame = new CMainFrame();
        Check(frame->Create(nullptr, L"Isolated idle-close fixture", WS_OVERLAPPEDWINDOW,
                            CRect(0, 0, 1280, 840)) != FALSE, "fresh frame starts after previous owner closes");
        const HWND idle_window = frame->GetSafeHwnd();
        test_app.m_pMainWnd = frame;
        SendMessageW(idle_window, WM_CLOSE, 0, 0);
        PumpUntil([&] { return !IsWindow(idle_window); });
        test_app.m_pMainWnd = nullptr;
    }
private:
    static void sdk_result(CMainFrame::WorkerResult& result) {
        rl78_programmer::sdk::JobStatus status;
        status.job_uuid[0] = 1;
        status.operation_state = PLP_OPERATION_FAILED;
        status.terminal_result = PLP_TERMINAL_OUTCOME_UNKNOWN;
        status.error = PLP_ERR_USB_TIMEOUT;
        result.job = status;
        result.message = L"Programming OUTCOME_UNKNOWN | synthetic lost transport";
    }
};

int wmain() {
    if (!AfxWinInit(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), 0)) return 2;
    try {
        CMainFrameContractTest::Run();
        std::cout << "Native UI contract checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
