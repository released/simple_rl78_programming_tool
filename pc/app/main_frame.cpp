#include "main_frame.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>

#include "resource.h"
#include "rl78_programmer/integrity.h"
#include "rl78_programmer/intel_hex.h"
#include "rl78_programmer/plp_contract.h"

namespace {

constexpr UINT kWorkerProgressMessage = WM_APP + 100;
constexpr UINT kWorkerDoneMessage = WM_APP + 101;
constexpr std::array<std::wstring_view, 5> kBridgeWorkflowLabels = {
    L"Step 1  Browse + Load + Validate",
    L"Step 2  Return Bootloader",
    L"Step 3  Scan Bootloader",
    L"Step 4  Upload Firmware",
    L"Step 5  Run Application"
};
constexpr std::array<UINT, 5> kBridgeWorkflowControlIds = {
    IDC_BRIDGE_FW_STEP_1,
    IDC_BRIDGE_FW_STEP_2,
    IDC_BRIDGE_FW_STEP_3,
    IDC_BRIDGE_FW_STEP_4,
    IDC_BRIDGE_FW_STEP_5
};
constexpr std::array<std::wstring_view, 5> kOfflineWorkflowLabels = {
    L"Step 1  Browse + Load + Validate (auto-generate OIP)",
    L"Step 2  Download to Board Storage (Nu-Link2-Me: SPI Flash)",
    L"Step 3  Activate Downloaded Package",
    L"Step 4  Arm Offline Mode",
    L"Step 5  Trigger on board (Nu-Link2-Me KEY1 / M032 RESET)"
};
constexpr std::array<UINT, 5> kOfflineWorkflowControlIds = {
    IDC_OFFLINE_STEP_1,
    IDC_OFFLINE_STEP_2,
    IDC_OFFLINE_STEP_3,
    IDC_OFFLINE_STEP_4,
    IDC_OFFLINE_STEP_5
};

void SafeMove(CWnd& control, int x, int y, int width, int height) {
    if (control.GetSafeHwnd() != nullptr) {
        control.MoveWindow(x, y, (std::max)(width, 1), (std::max)(height, 1));
    }
}

void SafeShow(CWnd& control, bool visible) {
    if (control.GetSafeHwnd() != nullptr) {
        control.ShowWindow(visible ? SW_SHOW : SW_HIDE);
    }
}

std::wstring HexDigest(const std::uint8_t* data, std::size_t size) {
    std::wostringstream out;
    out << std::uppercase << std::hex << std::setfill(L'0');
    for (std::size_t i = 0; i < size; ++i) {
        out << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return out.str();
}

std::wstring NarrowToWide(const char* text) {
    if (text == nullptr || *text == '\0') return {};
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                                             nullptr, 0);
    if (required <= 1) {
        std::wstring fallback;
        while (*text != '\0') {
            fallback.push_back(static_cast<unsigned char>(*text));
            ++text;
        }
        return fallback;
    }
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                        value.data(), required);
    value.resize(static_cast<std::size_t>(required - 1));
    return value;
}

std::vector<std::uint8_t> ReadFileBytes(const std::wstring& path) {
    std::ifstream stream(std::filesystem::path(path), std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open input file");
    }
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) {
        throw std::runtime_error("cannot determine input file size");
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(length));
    if (!data.empty()) {
        stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!stream) {
        throw std::runtime_error("failed to read input file");
    }
    return data;
}

std::wstring NowText() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t text[64] = {};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond);
    return text;
}

std::wstring UtcNowText() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    wchar_t text[64] = {};
    swprintf_s(text, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return text;
}

std::wstring SingleLine(std::wstring_view text) {
    constexpr std::size_t kMaximumLength = 8192;
    std::wstring value;
    value.reserve((std::min)(text.size(), kMaximumLength));
    for (const wchar_t character : text) {
        const wchar_t* replacement = nullptr;
        if (character == L'\r') replacement = L"\\r";
        else if (character == L'\n') replacement = L"\\n";
        else if (character == L'\t') replacement = L"\\t";
        if (replacement != nullptr) {
            value += replacement;
        } else if (character < 0x20) {
            value.push_back(L'?');
        } else {
            value.push_back(character);
        }
        if (value.size() >= kMaximumLength) {
            value.resize(kMaximumLength);
            value += L"...[truncated]";
            break;
        }
    }
    return value;
}

std::filesystem::path ExecutableDirectory() {
    std::vector<wchar_t> buffer(512, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            throw std::runtime_error("cannot determine executable path");
        }
        if (length < buffer.size()) {
            return std::filesystem::path(
                       std::wstring(buffer.data(), static_cast<std::size_t>(length)))
                .parent_path();
        }
        if (buffer.size() >= 32768) {
            throw std::runtime_error("executable path is too long");
        }
        buffer.resize(buffer.size() * 2, L'\0');
    }
}

std::wstring LogFileBaseName() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t text[128] = {};
    swprintf_s(text,
               L"Rl78ProgrammerTool_%04u%02u%02u_%02u%02u%02u_%03u_%lu",
               time.wYear, time.wMonth, time.wDay,
               time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
               static_cast<unsigned long>(GetCurrentProcessId()));
    return text;
}

std::wstring HexDetail(std::uint16_t value) {
    std::wostringstream out;
    out << L"0x" << std::uppercase << std::hex << std::setw(4)
        << std::setfill(L'0') << value;
    return out.str();
}

std::wstring HexValue(std::uint32_t value, int width) {
    std::wostringstream out;
    out << L"0x" << std::uppercase << std::hex << std::setw(width)
        << std::setfill(L'0') << value;
    return out.str();
}

std::wstring StageText(std::uint8_t stage) {
    return std::to_wstring(stage) + L" (" +
           rl78_programmer::sdk::StageName(stage) + L")";
}

std::wstring KibibyteText(std::uint64_t bytes) {
    std::wostringstream out;
    out << std::fixed << std::setprecision(2)
        << (static_cast<double>(bytes) / 1024.0) << L" KiB";
    return out.str();
}

std::wstring ProgressIndicatorText(std::uint16_t basis_points,
                                   std::uint64_t loaded_bytes,
                                   std::uint64_t load_total_bytes,
                                   std::uint64_t programmed_bytes,
                                   std::uint64_t program_total_bytes) {
    const std::uint16_t clamped_basis_points =
        (std::min)(basis_points, static_cast<std::uint16_t>(10000u));
    std::wostringstream out;
    out << std::fixed << std::setprecision(2)
        << (static_cast<double>(clamped_basis_points) / 100.0)
        << L"% | Loaded ";
    if (load_total_bytes == 0u) {
        out << L"--";
    } else {
        out << KibibyteText((std::min)(loaded_bytes, load_total_bytes))
            << L" / " << KibibyteText(load_total_bytes);
    }
    out << L" | Programmed ";
    if (program_total_bytes == 0u) {
        out << L"-- | Remaining --";
    } else {
        const std::uint64_t completed =
            (std::min)(programmed_bytes, program_total_bytes);
        out << KibibyteText(completed)
            << L" | Remaining " << KibibyteText(program_total_bytes - completed);
    }
    return out.str();
}

std::wstring JobTerminalText(
    const std::wstring& mode,
    const rl78_programmer::sdk::JobStatus& job) {
    std::wostringstream out;
    out << L"event=JOB_TERMINAL"
        << L" mode=" << mode
        << L" job=" << rl78_programmer::sdk::FormatUuid(job.job_uuid)
        << L" result=" << rl78_programmer::sdk::TerminalResultName(job.terminal_result)
        << L" stage=" << rl78_programmer::sdk::StageName(job.stage)
        << L"(" << static_cast<unsigned>(job.stage) << L")"
        << L" error=" << rl78_programmer::sdk::ErrorName(job.error)
        << L" detail=" << HexDetail(job.detail)
        << L" attempt=" << job.attempt
        << L" elapsed_ms=" << job.elapsed_ms
        << L" progress_bps=" << job.progress_basis_points;
    return out.str();
}

std::wstring OfflineResultText(
    const rl78_programmer::sdk::OfflineResult& result) {
    std::wostringstream out;
    out << L"event=OFFLINE_RESULT"
        << L" job=" << rl78_programmer::sdk::FormatUuid(result.job_uuid)
        << L" result="
        << rl78_programmer::sdk::TerminalResultName(result.terminal_result)
        << L" stage=" << rl78_programmer::sdk::StageName(result.stage)
        << L"(" << static_cast<unsigned>(result.stage) << L")"
        << L" error=" << rl78_programmer::sdk::ErrorName(result.error)
        << L" detail=" << HexDetail(result.detail)
        << L" attempt=" << result.attempt
        << L" elapsed_ms=" << result.elapsed_ms;
    return out.str();
}

std::wstring OfflineFailureSnapshotText(
    const rl78_programmer::sdk::OfflineResult& result) {
    const auto& snapshot = result.failure;
    const auto flags = snapshot.pin_flags;
    std::wostringstream out;
    out << L"event=OFFLINE_FAILURE_SNAPSHOT"
        << L" schema=" << static_cast<unsigned>(snapshot.version)
        << L" job=" << rl78_programmer::sdk::FormatUuid(result.job_uuid)
        << L" step=" << static_cast<unsigned>(snapshot.protocol_step)
        << L" unit_byte=" << static_cast<unsigned>(snapshot.unit_byte_index)
        << L" address=" << HexValue(snapshot.address, 8)
        << L" tx=" << HexValue(snapshot.tx_value, 2)
        << L" tx_bit=" << static_cast<unsigned>(snapshot.tx_bit_index)
        << L" timer_state=" << static_cast<unsigned>(snapshot.timer_state)
        << L" timer_count=" << snapshot.timer_count
        << L" pa9_dout="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA9_DOUT) != 0u ? 1 : 0)
        << L" pa9_pin="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA9_INPUT) != 0u ? 1 : 0)
        << L" pa10_pin="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA10_INPUT) != 0u ? 1 : 0)
        << L" pa10_irq="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA10_IRQ) != 0u ? 1 : 0)
        << L" tx_busy="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_TX_BUSY) != 0u ? 1 : 0)
        << L" rx_enabled="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_RX_ENABLED) != 0u ? 1 : 0)
        << L" destructive="
        << ((flags & PLP_OFFLINE_FAILURE_DESTRUCTIVE) != 0u ? 1 : 0)
        << L" link_valid="
        << ((flags & PLP_OFFLINE_FAILURE_LINK_VALID) != 0u ? 1 : 0);
    return out.str();
}

std::wstring OfflineFailureHistoryText(
    const rl78_programmer::sdk::OfflineFailureRecord& record) {
    const auto& snapshot = record.failure;
    const auto flags = snapshot.pin_flags;
    std::wostringstream out;
    out << L"event=OFFLINE_FAILURE_HISTORY"
        << L" schema=" << static_cast<unsigned>(record.schema)
        << L" sequence=" << record.sequence
        << L" job_counter=" << record.job_counter
        << L" captured_uptime_ms=" << record.captured_uptime_ms
        << L" result="
        << rl78_programmer::sdk::TerminalResultName(record.terminal_result)
        << L" stage=" << rl78_programmer::sdk::StageName(record.stage)
        << L"(" << static_cast<unsigned>(record.stage) << L")"
        << L" error=" << rl78_programmer::sdk::ErrorName(record.error)
        << L" detail=" << HexDetail(record.detail)
        << L" attempt=" << record.attempt
        << L" snapshot_valid=" << (snapshot.valid ? 1 : 0)
        << L" snapshot_schema=" << static_cast<unsigned>(snapshot.version)
        << L" step=" << static_cast<unsigned>(snapshot.protocol_step)
        << L" unit_byte=" << static_cast<unsigned>(snapshot.unit_byte_index)
        << L" address=" << HexValue(snapshot.address, 8)
        << L" tx=" << HexValue(snapshot.tx_value, 2)
        << L" tx_bit=" << static_cast<unsigned>(snapshot.tx_bit_index)
        << L" timer_state=" << static_cast<unsigned>(snapshot.timer_state)
        << L" timer_count=" << snapshot.timer_count
        << L" pa9_dout="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA9_DOUT) != 0u ? 1 : 0)
        << L" pa9_pin="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA9_INPUT) != 0u ? 1 : 0)
        << L" pa10_pin="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA10_INPUT) != 0u ? 1 : 0)
        << L" pa10_irq="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_PA10_IRQ) != 0u ? 1 : 0)
        << L" tx_busy="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_TX_BUSY) != 0u ? 1 : 0)
        << L" rx_enabled="
        << ((flags & PLP_OFFLINE_FAILURE_PIN_RX_ENABLED) != 0u ? 1 : 0)
        << L" destructive="
        << ((flags & PLP_OFFLINE_FAILURE_DESTRUCTIVE) != 0u ? 1 : 0)
        << L" link_valid="
        << ((flags & PLP_OFFLINE_FAILURE_LINK_VALID) != 0u ? 1 : 0);
    return out.str();
}

} // namespace

BEGIN_MESSAGE_MAP(CVerticallyCenteredEdit, CEdit)
    ON_WM_SIZE()
    ON_WM_CHAR()
    ON_MESSAGE(WM_SETFONT, &CVerticallyCenteredEdit::OnSetFont)
END_MESSAGE_MAP()

void CVerticallyCenteredEdit::UpdateTextRectangle() {
    if (GetSafeHwnd() == nullptr) return;

    CRect bounds;
    GetClientRect(&bounds);
    if (bounds.IsRectEmpty()) return;

    CClientDC dc(this);
    CFont* old_font = nullptr;
    if (GetFont() != nullptr) old_font = dc.SelectObject(GetFont());

    TEXTMETRICW metrics{};
    dc.GetTextMetrics(&metrics);
    if (old_font != nullptr) dc.SelectObject(old_font);

    const UINT dpi = GetDpiForWindow(GetSafeHwnd());
    const int horizontal_padding = MulDiv(4, static_cast<int>(dpi), 96);
    const int available_height = (std::max)(0, bounds.Height() - 2);
    const int font_height = static_cast<int>(metrics.tmHeight);
    const int text_height = (std::min)(available_height, font_height + 2);
    const int top = (std::max)(0, (bounds.Height() - text_height) / 2);
    bounds.left += horizontal_padding;
    bounds.right -= horizontal_padding;
    bounds.top = top;
    bounds.bottom = top + text_height;
    SendMessage(EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&bounds));
    Invalidate(FALSE);
}

void CVerticallyCenteredEdit::OnSize(UINT type, int cx, int cy) {
    CEdit::OnSize(type, cx, cy);
    UpdateTextRectangle();
}

void CVerticallyCenteredEdit::OnChar(UINT character,
                                     UINT repeat_count,
                                     UINT flags) {
    if (character == L'\r' || character == L'\n') return;
    CEdit::OnChar(character, repeat_count, flags);
}

LRESULT CVerticallyCenteredEdit::OnSetFont(WPARAM font, LPARAM redraw) {
    const LRESULT result = DefWindowProc(WM_SETFONT, font, redraw);
    UpdateTextRectangle();
    return result;
}

void CVerticallyCenteredComboBox::DrawItem(LPDRAWITEMSTRUCT draw_item) {
    if (draw_item == nullptr || draw_item->hDC == nullptr) return;
    CDC* dc = CDC::FromHandle(draw_item->hDC);
    if (dc == nullptr) return;

    const bool selected = (draw_item->itemState & ODS_SELECTED) != 0u;
    const bool disabled = (draw_item->itemState & ODS_DISABLED) != 0u;
    const COLORREF background = GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    const COLORREF foreground = GetSysColor(
        disabled ? COLOR_GRAYTEXT : (selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    CRect bounds(draw_item->rcItem);
    dc->FillSolidRect(bounds, background);
    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(foreground);

    CString text;
    if (draw_item->itemID == static_cast<UINT>(-1)) {
        GetWindowTextW(text);
    } else {
        GetLBText(static_cast<int>(draw_item->itemID), text);
    }
    CFont* old_font = nullptr;
    if (GetFont() != nullptr) old_font = dc->SelectObject(GetFont());
    bounds.DeflateRect(6, 0);
    dc->DrawTextW(text, bounds,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                      DT_END_ELLIPSIS | DT_NOPREFIX);
    if (old_font != nullptr) dc->SelectObject(old_font);
    if ((draw_item->itemState & ODS_FOCUS) != 0u) {
        dc->DrawFocusRect(&draw_item->rcItem);
    }
}

void CVerticallyCenteredComboBox::MeasureItem(LPMEASUREITEMSTRUCT measure_item) {
    if (measure_item == nullptr) return;
    const UINT dpi = GetSafeHwnd() != nullptr ? GetDpiForWindow(GetSafeHwnd()) : 96u;
    measure_item->itemHeight = static_cast<UINT>(
        MulDiv(26, static_cast<int>(dpi), 96));
}

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_WM_CTLCOLOR()
    ON_NOTIFY(TCN_SELCHANGE, IDC_MAIN_TAB, &CMainFrame::OnTabChanged)
    ON_BN_CLICKED(IDC_SCAN_BUTTON, &CMainFrame::OnScan)
    ON_BN_CLICKED(IDC_CONNECT_BUTTON, &CMainFrame::OnConnect)
    ON_BN_CLICKED(IDC_DISCONNECT_BUTTON, &CMainFrame::OnDisconnect)
    ON_BN_CLICKED(IDC_RESET_MCU_BUTTON, &CMainFrame::OnResetMcu)
    ON_CBN_SELCHANGE(IDC_BOARD_PROFILE, &CMainFrame::OnBoardProfileChanged)
    ON_BN_CLICKED(IDC_MOT_BROWSE, &CMainFrame::OnBrowseMot)
    ON_EN_CHANGE(IDC_MOT_PATH, &CMainFrame::OnMotPathChanged)
    ON_BN_CLICKED(IDC_MOT_LOAD, &CMainFrame::OnLoadMot)
    ON_CBN_SELCHANGE(IDC_TARGET_PROFILE, &CMainFrame::OnTargetProfileChanged)
    ON_BN_CLICKED(IDC_READ_SIGNATURE, &CMainFrame::OnReadSignature)
    ON_BN_CLICKED(IDC_PROGRAM_ONLINE, &CMainFrame::OnProgramOnline)
    ON_BN_CLICKED(IDC_CANCEL_JOB, &CMainFrame::OnCancelJob)
    ON_BN_CLICKED(IDC_OIP_SAVE, &CMainFrame::OnSaveOip)
    ON_BN_CLICKED(IDC_OIP_DOWNLOAD, &CMainFrame::OnDownloadOip)
    ON_BN_CLICKED(IDC_OIP_ACTIVATE, &CMainFrame::OnActivateOip)
    ON_BN_CLICKED(IDC_PROGRAM_OFFLINE, &CMainFrame::OnProgramOffline)
    ON_BN_CLICKED(IDC_ARM_OFFLINE, &CMainFrame::OnArmOffline)
    ON_BN_CLICKED(IDC_SWITCH_ONLINE, &CMainFrame::OnSwitchOnline)
    ON_BN_CLICKED(IDC_BRIDGE_FW_BROWSE, &CMainFrame::OnBrowseBridgeFirmware)
    ON_CBN_SELCHANGE(IDC_BRIDGE_FW_PROFILE, &CMainFrame::OnBridgeFirmwareProfileChanged)
    ON_BN_CLICKED(IDC_BRIDGE_FW_LOAD, &CMainFrame::OnLoadBridgeFirmware)
    ON_BN_CLICKED(IDC_BRIDGE_FW_RETURN_BOOT, &CMainFrame::OnReturnBootloader)
    ON_BN_CLICKED(IDC_BRIDGE_FW_SCAN_BOOT, &CMainFrame::OnScanBridgeBootloader)
    ON_BN_CLICKED(IDC_BRIDGE_FW_UPLOAD, &CMainFrame::OnUploadBridgeFirmware)
    ON_BN_CLICKED(IDC_BRIDGE_FW_RUN, &CMainFrame::OnRunBridgeApplication)
    ON_BN_CLICKED(IDC_BRIDGE_FW_CANCEL, &CMainFrame::OnCancelBridgeFirmware)
    ON_BN_CLICKED(IDC_HISTORY_CLEAR, &CMainFrame::OnClearHistory)
    ON_BN_CLICKED(IDC_LOG_CLEAR, &CMainFrame::OnClearLog)
    ON_BN_CLICKED(IDC_LOG_FILE_ENABLE, &CMainFrame::OnToggleFileLog)
    ON_MESSAGE(kWorkerProgressMessage, &CMainFrame::OnWorkerProgress)
    ON_MESSAGE(kWorkerDoneMessage, &CMainFrame::OnWorkerDone)
    ON_WM_CLOSE()
END_MESSAGE_MAP()

CMainFrame::CMainFrame() = default;

CMainFrame::~CMainFrame() {
    cancel_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs) {
    if (!CFrameWnd::PreCreateWindow(cs)) return FALSE;
    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
    cs.lpszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                       LoadCursor(nullptr, IDC_ARROW),
                                       reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
                                       AfxGetApp()->LoadIcon(IDR_MAINFRAME));
    return TRUE;
}

int CMainFrame::OnCreate(LPCREATESTRUCT create_struct) {
    if (CFrameWnd::OnCreate(create_struct) == -1) return -1;
    CreateControls();
    ApplyFont();
    LoadConfigToUi();
    SetStatus(L"Disconnected");
    AppendLog(L"RL78 Programmer Tool started. Select the exact G15/G16 flash-size profile before loading an image.");
    UpdateUiState();
    CRect client;
    GetClientRect(&client);
    LayoutControls(client.Width(), client.Height());
    return 0;
}

void CMainFrame::CreateControls() {
    const DWORD label_style = WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE;
    const DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE |
                             ES_AUTOHSCROLL;
    const DWORD left_edit_style = edit_style | ES_LEFT;
    const DWORD button_style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

    vid_label_.Create(L"VID", label_style, {}, this);
    vid_edit_.Create(left_edit_style, {}, this, IDC_VID_EDIT);
    pid_label_.Create(L"PID", label_style, {}, this);
    pid_edit_.Create(left_edit_style, {}, this, IDC_PID_EDIT);
    scan_button_.Create(L"Scan", button_style, {}, this, IDC_SCAN_BUTTON);
    device_combo_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST |
                             CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                         {}, this, IDC_DEVICE_COMBO);
    connect_button_.Create(L"Connect", button_style, {}, this, IDC_CONNECT_BUTTON);
    disconnect_button_.Create(L"Disconnect", button_style, {}, this, IDC_DISCONNECT_BUTTON);
    reset_mcu_button_.Create(L"RESET MCU", button_style, {}, this, IDC_RESET_MCU_BUTTON);
    status_text_.Create(L"Disconnected", label_style | SS_SUNKEN, {}, this, IDC_STATUS_TEXT);
    device_info_.Create(L"No programmer selected", label_style, {}, this, IDC_DEVICE_INFO);
    board_profile_label_.Create(L"Programmer board", label_style, {}, this);
    board_profile_combo_.Create(
        WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL,
        {}, this, IDC_BOARD_PROFILE);
    board_profile_combo_.AddString(L"Nu-Link2-Me (USB HID)");
    board_profile_combo_.AddString(L"NuMaker-M032SE (USB HID)");
    board_profile_combo_.AddString(L"M032 TEST BOARD");
    board_profile_combo_.SetCurSel(0);

    tab_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_TABS, {}, this, IDC_MAIN_TAB);
    tab_.InsertItem(0, L"Program");
    tab_.InsertItem(1, L"Offline");
    tab_.InsertItem(2, L"FW Upload (USB HID)");
    tab_.InsertItem(3, L"History");
    tab_.InsertItem(4, L"Log");

    target_profile_label_.Create(L"Target profile", label_style, {}, this);
    target_profile_combo_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER |
                                     CBS_DROPDOWNLIST | WS_VSCROLL,
                                 {}, this, IDC_TARGET_PROFILE);
    target_profile_combo_.AddString(L"RL78/G15 4 KB");
    target_profile_combo_.AddString(L"RL78/G15 8 KB");
    target_profile_combo_.AddString(L"RL78/G16 16 KB");
    target_profile_combo_.AddString(L"RL78/G16 32 KB");
    target_profile_combo_.SetCurSel(1);
    mot_label_.Create(L"Firmware file", label_style, {}, this);
    mot_path_.Create(edit_style, {}, this, IDC_MOT_PATH);
    mot_browse_.Create(L"Browse...", button_style, {}, this, IDC_MOT_BROWSE);
    mot_load_.Create(L"Load + Validate", button_style, {}, this, IDC_MOT_LOAD);
    image_summary_.Create(L"No validated image", label_style | SS_SUNKEN, {}, this, IDC_IMAGE_SUMMARY);
    signature_button_.Create(L"Profile checked before erase", button_style, {}, this, IDC_READ_SIGNATURE);
    online_button_.Create(L"Program Online", button_style, {}, this, IDC_PROGRAM_ONLINE);
    cancel_button_.Create(L"Cancel (safe point)", button_style, {}, this, IDC_CANCEL_JOB);
    progress_.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, {}, this, IDC_PROGRAM_PROGRESS);
    progress_.SetRange32(0, 10000);
    program_progress_text_.Create(
        L"0.00% | Loaded -- | Programmed -- | Remaining --",
        label_style, {}, this, IDC_PROGRAM_PROGRESS_TEXT);
    program_status_.Create(L"Idle", label_style | SS_SUNKEN, {}, this, IDC_PROGRAM_STATUS);

    oip_save_.Create(L"Save OIP... (optional)", button_style, {}, this, IDC_OIP_SAVE);
    oip_download_.Create(L"Download to Board Storage", button_style, {}, this, IDC_OIP_DOWNLOAD);
    oip_activate_.Create(L"Activate Package", button_style, {}, this, IDC_OIP_ACTIVATE);
    offline_button_.Create(L"Test Active Package (HID)", button_style, {}, this, IDC_PROGRAM_OFFLINE);
    operating_mode_text_.Create(L"Bridge mode: unknown", label_style | SS_SUNKEN,
                                {}, this, IDC_OPERATING_MODE);
    arm_offline_button_.Create(L"Arm Offline", button_style,
                               {}, this, IDC_ARM_OFFLINE);
    switch_online_button_.Create(L"Switch to Online Only", button_style,
                                 {}, this, IDC_SWITCH_ONLINE);
    offline_progress_.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, {}, this,
                             IDC_OFFLINE_PROGRESS);
    offline_progress_.SetRange32(0, 10000);
    offline_progress_text_.Create(
        L"0.00% | Loaded -- | Programmed -- | Remaining --",
        label_style, {}, this, IDC_OFFLINE_PROGRESS_TEXT);
    for (std::size_t i = 0; i < offline_steps_.size(); ++i) {
        offline_steps_[i].Create(
            kOfflineWorkflowLabels[i].data(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT | SS_CENTERIMAGE,
            {}, this, kOfflineWorkflowControlIds[i]);
    }
    offline_status_.Create(
        L"Step 1: select the target profile and firmware, then click Load + Validate.",
        label_style | SS_SUNKEN,
                           {}, this, IDC_OFFLINE_STATUS);
    for (std::size_t i = 0; i < offline_steps_.size(); ++i) {
        SetOfflineWorkflowState(static_cast<OfflineWorkflowStep>(i),
                                WorkflowState::Pending);
    }

    bridge_fw_profile_label_.Create(L"Bridge profile", label_style, {}, this);
    bridge_fw_profile_combo_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER |
                                        CBS_DROPDOWNLIST | WS_VSCROLL,
                                    {}, this, IDC_BRIDGE_FW_PROFILE);
    bridge_fw_profile_combo_.AddString(L"M032SE3AE / NuMaker-M032SE");
    bridge_fw_profile_combo_.AddString(L"M484SIDAE / Nu-Link2-Me HS-USB");
    bridge_fw_profile_combo_.AddString(L"M032 TEST BOARD / UART ISP");
    bridge_fw_profile_combo_.SetCurSel(0);
    bridge_fw_path_label_.Create(L"Bridge APP BIN", label_style, {}, this);
    bridge_fw_path_.Create(edit_style, {}, this, IDC_BRIDGE_FW_PATH);
    bridge_fw_browse_.Create(L"Browse...", button_style, {}, this, IDC_BRIDGE_FW_BROWSE);
    bridge_fw_load_.Create(L"Load + Validate", button_style, {}, this, IDC_BRIDGE_FW_LOAD);
    bridge_fw_return_boot_.Create(L"Return Boot loader", button_style, {}, this,
                                  IDC_BRIDGE_FW_RETURN_BOOT);
    bridge_fw_scan_boot_.Create(L"Scan Bootloader", button_style, {}, this,
                                IDC_BRIDGE_FW_SCAN_BOOT);
    bridge_fw_device_combo_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER |
                                       CBS_DROPDOWNLIST | WS_VSCROLL,
                                   {}, this, IDC_BRIDGE_FW_DEVICE);
    bridge_fw_device_combo_.SetDroppedWidth(1200);
    bridge_fw_upload_.Create(L"Upload Firmware", button_style, {}, this,
                             IDC_BRIDGE_FW_UPLOAD);
    bridge_fw_run_.Create(L"Run Application", button_style, {}, this,
                          IDC_BRIDGE_FW_RUN);
    bridge_fw_cancel_.Create(L"Cancel", button_style, {}, this, IDC_BRIDGE_FW_CANCEL);
    bridge_fw_progress_.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, {}, this,
                               IDC_BRIDGE_FW_PROGRESS);
    bridge_fw_progress_.SetRange32(0, 10000);
    bridge_fw_progress_text_.Create(
        L"0.00% | Loaded -- | Programmed -- | Remaining --",
        label_style, {}, this, IDC_BRIDGE_FW_PROGRESS_TEXT);
    for (std::size_t i = 0; i < bridge_fw_steps_.size(); ++i) {
        bridge_fw_steps_[i].Create(
            kBridgeWorkflowLabels[i].data(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT | SS_CENTERIMAGE,
            {}, this, kBridgeWorkflowControlIds[i]);
    }
    bridge_fw_status_.Create(
        L"Load the fixed-size bridge application BIN generated by SRecord.",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_SUNKEN,
        {}, this, IDC_BRIDGE_FW_STATUS);
    for (std::size_t i = 0; i < bridge_fw_steps_.size(); ++i) {
        SetBridgeWorkflowState(static_cast<BridgeWorkflowStep>(i),
                               WorkflowState::Pending);
    }

    history_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
                    {}, this, IDC_HISTORY_LIST);
    history_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    history_.InsertColumn(0, L"Time", LVCFMT_LEFT, 155);
    history_.InsertColumn(1, L"Mode", LVCFMT_LEFT, 90);
    history_.InsertColumn(2, L"Result", LVCFMT_LEFT, 90);
    history_.InsertColumn(3, L"Stage", LVCFMT_LEFT, 160);
    history_.InsertColumn(4, L"Error", LVCFMT_LEFT, 300);
    history_.InsertColumn(5, L"Detail", LVCFMT_LEFT, 90);
    history_clear_.Create(L"Clear History", button_style, {}, this, IDC_HISTORY_CLEAR);

    log_edit_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
                         ES_READONLY | WS_VSCROLL,
                     {}, this, IDC_LOG_EDIT);
    log_clear_.Create(L"Clear Log", button_style, {}, this, IDC_LOG_CLEAR);
    log_file_enable_.Create(L"Save log to file",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                            {}, this, IDC_LOG_FILE_ENABLE);
    log_file_enable_.SetCheck(BST_UNCHECKED);
    log_file_status_.Create(L"File logging disabled",
                            label_style | SS_SUNKEN | SS_PATHELLIPSIS,
                            {}, this);
    ShowSelectedTab();
}

void CMainFrame::ApplyFont() {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    ui_font_.CreateFontIndirectW(&metrics.lfMessageFont);
    CWnd* controls[] = {
        &vid_label_, &vid_edit_, &pid_label_, &pid_edit_, &scan_button_, &device_combo_,
        &connect_button_, &disconnect_button_, &reset_mcu_button_, &status_text_,
        &device_info_, &board_profile_label_, &board_profile_combo_, &tab_,
        &target_profile_label_, &target_profile_combo_, &mot_label_, &mot_path_,
        &mot_browse_, &mot_load_, &image_summary_, &signature_button_,
        &online_button_, &cancel_button_, &program_progress_text_, &program_status_,
        &oip_save_,
        &oip_download_, &oip_activate_, &offline_button_, &operating_mode_text_,
        &arm_offline_button_, &switch_online_button_, &offline_progress_text_,
        &offline_status_, &history_,
        &bridge_fw_profile_label_, &bridge_fw_profile_combo_, &bridge_fw_path_label_,
        &bridge_fw_path_, &bridge_fw_browse_, &bridge_fw_load_, &bridge_fw_return_boot_,
        &bridge_fw_scan_boot_, &bridge_fw_device_combo_, &bridge_fw_upload_,
        &bridge_fw_run_, &bridge_fw_cancel_, &bridge_fw_progress_text_, &bridge_fw_status_,
        &history_clear_, &log_edit_, &log_clear_, &log_file_enable_,
        &log_file_status_
    };
    for (auto* control : controls) {
        if (control->GetSafeHwnd() != nullptr) control->SetFont(&ui_font_);
    }
    if (device_combo_.GetSafeHwnd() != nullptr) {
        const UINT dpi = GetDpiForWindow(device_combo_.GetSafeHwnd());
        const UINT item_height = static_cast<UINT>(
            MulDiv(26, static_cast<int>(dpi), 96));
        device_combo_.SetItemHeight(-1, item_height);
        device_combo_.SetItemHeight(0, item_height);
    }
    for (auto& step : bridge_fw_steps_) {
        if (step.GetSafeHwnd() != nullptr) step.SetFont(&ui_font_);
    }
    for (auto& step : offline_steps_) {
        if (step.GetSafeHwnd() != nullptr) step.SetFont(&ui_font_);
    }
}

void CMainFrame::OnSize(UINT type, int cx, int cy) {
    CFrameWnd::OnSize(type, cx, cy);
    if (type != SIZE_MINIMIZED) LayoutControls(cx, cy);
}

void CMainFrame::OnGetMinMaxInfo(MINMAXINFO* min_max) {
    CFrameWnd::OnGetMinMaxInfo(min_max);
    if (min_max != nullptr) {
        min_max->ptMinTrackSize.x = 1020;
        min_max->ptMinTrackSize.y = 620;
    }
}

HBRUSH CMainFrame::OnCtlColor(CDC* dc, CWnd* window, UINT control_color) {
    HBRUSH brush = CFrameWnd::OnCtlColor(dc, window, control_color);
    if (dc == nullptr || window == nullptr) return brush;
    const auto workflow_brush = [dc](WorkflowState state) -> HBRUSH {
        dc->SetBkMode(OPAQUE);
        if (state == WorkflowState::Completed) {
            dc->SetTextColor(GetSysColor(COLOR_GRAYTEXT));
            dc->SetBkColor(GetSysColor(COLOR_BTNFACE));
            return GetSysColorBrush(COLOR_BTNFACE);
        }
        dc->SetBkColor(GetSysColor(COLOR_WINDOW));
        if (state == WorkflowState::InProgress || state == WorkflowState::Ready) {
            dc->SetTextColor(GetSysColor(COLOR_HOTLIGHT));
        } else if (state == WorkflowState::Failed) {
            dc->SetTextColor(RGB(192, 0, 0));
        } else {
            dc->SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        }
        return GetSysColorBrush(COLOR_WINDOW);
    };
    for (std::size_t i = 0; i < bridge_fw_steps_.size(); ++i) {
        if (window->GetSafeHwnd() != bridge_fw_steps_[i].GetSafeHwnd()) continue;
        return workflow_brush(bridge_fw_step_states_[i]);
    }
    for (std::size_t i = 0; i < offline_steps_.size(); ++i) {
        if (window->GetSafeHwnd() != offline_steps_[i].GetSafeHwnd()) continue;
        return workflow_brush(offline_step_states_[i]);
    }
    return brush;
}

void CMainFrame::LayoutControls(int cx, int cy) {
    if (tab_.GetSafeHwnd() == nullptr) return;
    const UINT dpi = GetDpiForWindow(GetSafeHwnd());
    const auto s = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    const int margin = s(10);
    const int row = s(28);
    const int gap = s(8);
    int x = margin;
    int y = margin;
    SafeMove(vid_label_, x, y, s(28), row); x += s(30);
    SafeMove(vid_edit_, x, y, s(76), row); x += s(76) + gap;
    SafeMove(pid_label_, x, y, s(28), row); x += s(30);
    SafeMove(pid_edit_, x, y, s(76), row); x += s(76) + gap;
    SafeMove(scan_button_, x, y, s(72), row); x += s(72) + gap;
    const int combo_width = (std::max)(s(210), cx - x - s(514));
    SafeMove(device_combo_, x, y, combo_width, s(300)); x += combo_width + gap;
    SafeMove(connect_button_, x, y, s(82), row); x += s(82) + gap;
    SafeMove(disconnect_button_, x, y, s(92), row); x += s(92) + gap;
    SafeMove(reset_mcu_button_, x, y, s(96), row); x += s(96) + gap;
    SafeMove(status_text_, x, y, (std::max)(s(120), cx - x - margin), row);

    y += row + gap;
    SafeMove(board_profile_label_, margin, y, s(120), row);
    SafeMove(board_profile_combo_, margin + s(126), y, s(270), s(160));
    SafeMove(device_info_, margin + s(406), y,
             (std::max)(s(200), cx - margin * 2 - s(406)), row);
    y += row + gap;
    const int tab_y = y;
    SafeMove(tab_, margin, tab_y, cx - margin * 2, cy - tab_y - margin);
    const int content_x = margin + s(18);
    const int content_y = tab_y + s(36);
    const int content_w = cx - content_x - margin - s(18);
    const int content_h = cy - content_y - margin - s(12);

    int py = content_y + gap;
    SafeMove(target_profile_label_, content_x, py, s(110), row);
    SafeMove(target_profile_combo_, content_x + s(116), py, s(260), s(160));
    py += row + gap;
    SafeMove(mot_label_, content_x, py, s(110), row);
    SafeMove(mot_path_, content_x + s(116), py, content_w - s(350), row);
    SafeMove(mot_browse_, content_x + content_w - s(226), py, s(100), row);
    SafeMove(mot_load_, content_x + content_w - s(118), py, s(118), row);
    py += row + gap;
    SafeMove(image_summary_, content_x, py, content_w, s(86));
    py += s(86) + gap;
    const int source_actions_y = py;
    const int workflow_gap = s(5);
    SafeMove(signature_button_, content_x, py, s(180), row);
    SafeMove(online_button_, content_x + s(190), py, s(140), row);
    SafeMove(cancel_button_, content_x + s(340), py, s(160), row);
    py += row + gap;
    SafeMove(progress_, content_x, py, content_w, s(22));
    py += s(22) + gap;
    SafeMove(program_progress_text_, content_x, py, content_w, row);
    py += row + gap;
    SafeMove(program_status_, content_x, py, content_w, (std::max)(row, content_y + content_h - py));

    py = source_actions_y;
    SafeMove(oip_save_, content_x, py, s(180), row);
    SafeMove(oip_download_, content_x + s(190), py, s(190), row);
    SafeMove(oip_activate_, content_x + s(390), py, s(160), row);
    SafeMove(offline_button_, content_x + s(560), py, s(230), row);
    py += row + gap;
    SafeMove(operating_mode_text_, content_x, py, content_w - s(400), row);
    SafeMove(arm_offline_button_, content_x + content_w - s(390), py, s(185), row);
    SafeMove(switch_online_button_, content_x + content_w - s(195), py, s(195), row);
    py += row + gap;
    SafeMove(offline_progress_, content_x, py, content_w, s(22));
    py += s(22) + gap;
    SafeMove(offline_progress_text_, content_x, py, content_w, row);
    py += row + gap;
    for (std::size_t i = 0; i < offline_steps_.size(); ++i) {
        SafeMove(offline_steps_[i], content_x, py, content_w, row);
        py += row;
        if (i + 1u < offline_steps_.size()) py += workflow_gap;
    }
    py += gap;
    SafeMove(offline_status_, content_x, py, content_w,
             (std::max)(row, content_y + content_h - py));

    py = content_y + gap;
    SafeMove(bridge_fw_profile_label_, content_x, py, s(110), row);
    SafeMove(bridge_fw_profile_combo_, content_x + s(116), py, s(330), s(160));
    py += row + gap;
    SafeMove(bridge_fw_path_label_, content_x, py, s(110), row);
    SafeMove(bridge_fw_path_, content_x + s(116), py, content_w - s(356), row);
    SafeMove(bridge_fw_browse_, content_x + content_w - s(232), py, s(104), row);
    SafeMove(bridge_fw_load_, content_x + content_w - s(120), py, s(120), row);
    py += row + gap;
    SafeMove(bridge_fw_return_boot_, content_x, py, s(165), row);
    SafeMove(bridge_fw_scan_boot_, content_x + s(175), py, s(145), row);
    SafeMove(bridge_fw_device_combo_, content_x + s(330), py,
             (std::max)(s(220), content_w - s(330)), s(220));
    py += row + gap;
    SafeMove(bridge_fw_upload_, content_x, py, s(150), row);
    SafeMove(bridge_fw_run_, content_x + s(160), py, s(150), row);
    SafeMove(bridge_fw_cancel_, content_x + s(320), py, s(100), row);
    py += row + gap;
    SafeMove(bridge_fw_progress_, content_x, py, content_w, s(22));
    py += s(22) + gap;
    SafeMove(bridge_fw_progress_text_, content_x, py, content_w, row);
    py += row + gap;
    for (std::size_t i = 0; i < bridge_fw_steps_.size(); ++i) {
        SafeMove(bridge_fw_steps_[i], content_x, py, content_w, row);
        py += row;
        if (i + 1u < bridge_fw_steps_.size()) py += workflow_gap;
    }
    py += gap;
    SafeMove(bridge_fw_status_, content_x, py, content_w,
             (std::max)(row, content_y + content_h - py));

    SafeMove(history_, content_x, content_y + gap, content_w, content_h - row - gap * 3);
    SafeMove(history_clear_, content_x, content_y + content_h - row - gap, s(120), row);
    SafeMove(log_edit_, content_x, content_y + gap, content_w, content_h - row - gap * 3);
    SafeMove(log_file_enable_, content_x, content_y + content_h - row - gap, s(150), row);
    SafeMove(log_file_status_, content_x + s(158), content_y + content_h - row - gap,
             (std::max)(s(120), content_w - s(274)), row);
    SafeMove(log_clear_, content_x + content_w - s(108),
             content_y + content_h - row - gap, s(108), row);
}

void CMainFrame::OnTabChanged(NMHDR*, LRESULT* result) {
    ShowSelectedTab();
    if (result) *result = 0;
}

CMainFrame::ProgrammerBoardProfile CMainFrame::SelectedBoardProfile() const {
    switch (board_profile_combo_.GetCurSel()) {
    case 1:
        return ProgrammerBoardProfile::NumakerM032Usb;
    case 2:
        return ProgrammerBoardProfile::M032TestBoardUart;
    case 0:
    default:
        return ProgrammerBoardProfile::Nulink2MeUsb;
    }
}

bool CMainFrame::IsUartBoardSelected() const {
    return SelectedBoardProfile() ==
           ProgrammerBoardProfile::M032TestBoardUart;
}

void CMainFrame::ApplyBoardProfile() {
    const bool uart = IsUartBoardSelected();
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    std::wstring title =
        uart ? L"FW Upload (UART ISP)" : L"FW Upload (USB HID)";
    item.pszText = title.data();
    tab_.SetItem(2, &item);

    switch (SelectedBoardProfile()) {
    case ProgrammerBoardProfile::Nulink2MeUsb:
        bridge_fw_profile_combo_.SetCurSel(1);
        offline_button_.SetWindowTextW(L"Test Active Package (HID)");
        break;
    case ProgrammerBoardProfile::NumakerM032Usb:
        bridge_fw_profile_combo_.SetCurSel(0);
        offline_button_.SetWindowTextW(L"Test Active Package (HID)");
        break;
    case ProgrammerBoardProfile::M032TestBoardUart:
        bridge_fw_profile_combo_.SetCurSel(2);
        offline_button_.SetWindowTextW(L"Test Active Package (UART)");
        break;
    }
    bridge_fw_status_.SetWindowTextW(
        uart
            ? L"M032 TEST BOARD uses the external 3.3 V VCOM at 115200 8-N-1. Select the application BIN, return to UART ISP, then scan the same COM port."
            : L"Load the fixed-size bridge application BIN generated by SRecord.");
    bridge_firmware_image_.reset();
    bootloader_devices_.clear();
    bridge_fw_device_combo_.ResetContent();
    for (std::size_t i = 0; i < offline_step_states_.size(); ++i) {
        SetOfflineWorkflowState(
            static_cast<OfflineWorkflowStep>(i),
            offline_step_states_[i]);
    }
}

void CMainFrame::OnBoardProfileChanged() {
    if (worker_running_.load()) {
        return;
    }
    if (client_.IsConnected()) {
        client_.Disconnect();
    }
    bridge_firmware_updater_.Disconnect();
    devices_.clear();
    device_combo_.ResetContent();
    operating_mode_ = {};
    active_package_uuid_.fill(0u);
    downloaded_package_uuid_.fill(0u);
    device_info_.SetWindowTextW(L"No programmer selected");
    SetStatus(L"Board profile changed; scan for a programmer");
    ApplyBoardProfile();
    UpdateOperatingModeText();
    UpdateUiState();
}

void CMainFrame::ShowSelectedTab() {
    const int selected = tab_.GetSafeHwnd() == nullptr ? 0 : tab_.GetCurSel();
    CWnd* target_image[] = {&target_profile_label_, &target_profile_combo_, &mot_label_,
                            &mot_path_, &mot_browse_, &mot_load_, &image_summary_};
    CWnd* program[] = {&signature_button_, &online_button_, &cancel_button_, &progress_,
                       &program_progress_text_, &program_status_};
    CWnd* offline[] = {&oip_save_, &oip_download_, &oip_activate_,
                       &offline_button_, &operating_mode_text_, &arm_offline_button_,
                       &switch_online_button_, &offline_progress_,
                       &offline_progress_text_, &offline_status_};
    CWnd* bridge_fw[] = {
        &bridge_fw_profile_label_, &bridge_fw_profile_combo_, &bridge_fw_path_label_,
        &bridge_fw_path_, &bridge_fw_browse_, &bridge_fw_load_, &bridge_fw_return_boot_,
        &bridge_fw_scan_boot_, &bridge_fw_device_combo_, &bridge_fw_upload_,
        &bridge_fw_run_, &bridge_fw_cancel_, &bridge_fw_progress_,
        &bridge_fw_progress_text_, &bridge_fw_status_};
    for (auto* control : target_image) SafeShow(*control, selected == 0 || selected == 1);
    for (auto* control : program) SafeShow(*control, selected == 0);
    for (auto* control : offline) SafeShow(*control, selected == 1);
    for (auto& step : offline_steps_) SafeShow(step, selected == 1);
    for (auto* control : bridge_fw) SafeShow(*control, selected == 2);
    for (auto& step : bridge_fw_steps_) SafeShow(step, selected == 2);
    SafeShow(history_, selected == 3);
    SafeShow(history_clear_, selected == 3);
    SafeShow(log_edit_, selected == 4);
    SafeShow(log_clear_, selected == 4);
    SafeShow(log_file_enable_, selected == 4);
    SafeShow(log_file_status_, selected == 4);
}

void CMainFrame::UpdateUiState() {
    const bool busy = worker_running_.load();
    const bool connected = client_.IsConnected();
    const bool uart = IsUartBoardSelected();
    const bool validated_source_matches =
        validated_image_source_.Matches(ControlText(mot_path_));
    const bool validated_package =
        package_.has_value() && validated_source_matches;
    const bool downloaded_matches_package = validated_package &&
        downloaded_package_uuid_ == package_->package_uuid;
    const bool active_matches_package = validated_package &&
        !IsZeroUuid(active_package_uuid_) &&
        active_package_uuid_ == package_->package_uuid;
    scan_button_.EnableWindow(!busy);
    board_profile_combo_.EnableWindow(!busy);
    vid_label_.EnableWindow(!busy && !uart);
    vid_edit_.EnableWindow(!busy && !uart);
    pid_label_.EnableWindow(!busy && !uart);
    pid_edit_.EnableWindow(!busy && !uart);
    connect_button_.EnableWindow(!busy && !connected && device_combo_.GetCurSel() >= 0);
    disconnect_button_.EnableWindow(!busy && connected);
    reset_mcu_button_.EnableWindow(!busy && connected &&
        (client_.Capabilities() & PLP_CAP_PROGRAMMER_RESET) != 0u);
    mot_path_.EnableWindow(!busy);
    mot_browse_.EnableWindow(!busy);
    mot_load_.EnableWindow(!busy && !ControlText(mot_path_).empty());
    target_profile_combo_.EnableWindow(!busy);
    signature_button_.EnableWindow(FALSE);
    online_button_.EnableWindow(!busy && connected && validated_package);
    cancel_button_.EnableWindow(busy);
    oip_save_.EnableWindow(!busy && validated_package);
    oip_download_.EnableWindow(!busy && connected && validated_package);
    oip_activate_.EnableWindow(!busy && connected && downloaded_matches_package);
    offline_button_.EnableWindow(!busy && connected && active_matches_package);
    arm_offline_button_.EnableWindow(!busy && connected &&
        active_matches_package &&
        operating_mode_.mode != PLP_OPERATING_MODE_OFFLINE_ARMED);
    switch_online_button_.EnableWindow(!busy && connected &&
        operating_mode_.mode == PLP_OPERATING_MODE_OFFLINE_ARMED);
    bridge_fw_profile_combo_.EnableWindow(FALSE);
    bridge_fw_browse_.EnableWindow(!busy);
    bridge_fw_load_.EnableWindow(!busy && !ControlText(bridge_fw_path_).empty());
    bridge_fw_return_boot_.EnableWindow(!busy && connected &&
        (client_.Capabilities() & PLP_CAP_PROGRAMMER_UPDATE) != 0u);
    bridge_fw_scan_boot_.EnableWindow(!busy);
    bridge_fw_device_combo_.EnableWindow(!busy && !bootloader_devices_.empty());
    bridge_fw_upload_.EnableWindow(!busy && bridge_firmware_image_.has_value() &&
        bridge_fw_device_combo_.GetCurSel() >= 0);
    bridge_fw_run_.EnableWindow(!busy && bridge_fw_device_combo_.GetCurSel() >= 0);
    bridge_fw_cancel_.EnableWindow(busy);
}

void CMainFrame::UpdateOperatingModeText() {
    std::wstring text;
    if (!client_.IsConnected()) {
        text = L"Bridge mode: disconnected";
    } else if (operating_mode_.mode == PLP_OPERATING_MODE_OFFLINE_ARMED) {
        if (connected_offline_button_) {
            text = L"Bridge mode: OFFLINE ARMED / physical button trigger";
        } else {
            text = operating_mode_.autorun_enabled ?
                L"Bridge mode: OFFLINE ARMED / Reset-triggered autorun" :
                L"Bridge mode: OFFLINE ARMED / manual trigger";
        }
    } else {
        text = L"Bridge mode: ONLINE ONLY";
    }
    if (operating_mode_.active_package_available) {
        text += L" | Active package " +
                rl78_programmer::sdk::FormatUuid(operating_mode_.active_package_uuid);
    }
    operating_mode_text_.SetWindowTextW(text.c_str());
}

void CMainFrame::SetStatus(const std::wstring& text) {
    if (status_text_.GetSafeHwnd() != nullptr) status_text_.SetWindowTextW(text.c_str());
}

void CMainFrame::AppendLogToView(const std::wstring& line) {
    if (log_edit_.GetSafeHwnd() == nullptr) return;
    log_edit_.SetSel(-1, -1);
    log_edit_.ReplaceSel(line.c_str(), FALSE);
    const int length = log_edit_.GetWindowTextLengthW();
    if (length > 500000) {
        log_edit_.SetSel(0, length - 400000);
        log_edit_.ReplaceSel(L"", FALSE);
    }
}

void CMainFrame::AppendLog(const std::wstring& text) {
    const std::wstring safe_text = SingleLine(text);
    AppendLogToView(NowText() + L"  " + safe_text + L"\r\n");
    if (session_file_logger_.IsOpen()) {
        std::wstring error;
        if (!session_file_logger_.AppendLine(
                UtcNowText() + L"  " + safe_text, &error)) {
            DisableFileLogAfterError(error);
        }
    }
}

void CMainFrame::DisableFileLogAfterError(const std::wstring& error) {
    session_file_logger_.Close();
    if (log_file_enable_.GetSafeHwnd() != nullptr) {
        log_file_enable_.SetCheck(BST_UNCHECKED);
    }
    if (log_file_status_.GetSafeHwnd() != nullptr) {
        log_file_status_.SetWindowTextW(L"File logging disabled after error");
    }
    const std::wstring message = L"File logging stopped: " + SingleLine(error);
    AppendLogToView(NowText() + L"  ERROR: " + message + L"\r\n");
    PersistFileLogPreference();
    if (!closing_.load()) {
        MessageBoxW(message.c_str(), L"RL78 Programmer Log", MB_OK | MB_ICONERROR);
    }
}

void CMainFrame::PersistFileLogPreference() {
    config_.save_log_to_file =
        log_file_enable_.GetSafeHwnd() != nullptr &&
        log_file_enable_.GetCheck() == BST_CHECKED;
    std::wstring error;
    if (!SaveAppConfig(config_, &error)) {
        const std::wstring message =
            L"Cannot save file logging preference: " + SingleLine(error);
        AppendLogToView(NowText() + L"  ERROR: " + message + L"\r\n");
        if (!closing_.load()) {
            MessageBoxW(message.c_str(), L"RL78 Programmer Configuration",
                        MB_OK | MB_ICONERROR);
        }
    }
}

void CMainFrame::AddHistory(const std::wstring& mode,
                            const rl78_programmer::sdk::JobStatus& job) {
    const int row = history_.InsertItem(0, NowText().c_str());
    history_.SetItemText(row, 1, mode.c_str());
    const std::wstring result =
        rl78_programmer::sdk::TerminalResultName(job.terminal_result);
    history_.SetItemText(row, 2, result.c_str());
    history_.SetItemText(row, 3, StageText(job.stage).c_str());
    history_.SetItemText(row, 4, rl78_programmer::sdk::ErrorName(job.error).c_str());
    history_.SetItemText(row, 5, HexDetail(job.detail).c_str());
}

void CMainFrame::LoadConfigToUi() {
    std::wstring error;
    LoadAppConfig(&config_, &error);
    std::wostringstream vid;
    std::wostringstream pid;
    vid << L"0x" << std::hex << std::uppercase << std::setw(4) << std::setfill(L'0') << config_.vendor_id;
    pid << L"0x" << std::hex << std::uppercase << std::setw(4) << std::setfill(L'0') << config_.product_id;
    vid_edit_.SetWindowTextW(vid.str().c_str());
    pid_edit_.SetWindowTextW(pid.str().c_str());
    mot_path_.SetWindowTextW(config_.last_mot_path.c_str());
    bridge_fw_path_.SetWindowTextW(config_.last_bridge_firmware_path.c_str());
    board_profile_combo_.SetCurSel(config_.board_profile);
    ApplyBoardProfile();
    log_file_enable_.SetCheck(config_.save_log_to_file ?
                                  BST_CHECKED : BST_UNCHECKED);
    if (config_.save_log_to_file) {
        OnToggleFileLog();
    }
}

void CMainFrame::SaveConfigFromUi() {
    config_.vendor_id = ReadHexControl(vid_edit_, config_.vendor_id);
    config_.product_id = ReadHexControl(pid_edit_, config_.product_id);
    config_.last_mot_path = ControlText(mot_path_);
    config_.last_bridge_firmware_path = ControlText(bridge_fw_path_);
    config_.board_profile =
        (std::max)(0, board_profile_combo_.GetCurSel());
    config_.bridge_firmware_profile =
        (std::max)(0, bridge_fw_profile_combo_.GetCurSel());
    config_.save_log_to_file =
        log_file_enable_.GetCheck() == BST_CHECKED;
    std::wstring error;
    if (!SaveAppConfig(config_, &error)) AppendLog(error);
}

std::uint16_t CMainFrame::ReadHexControl(const CEdit& edit, std::uint16_t fallback) const {
    try {
        const auto text = ControlText(edit);
        std::size_t used = 0;
        const auto value = std::stoul(text, &used, 0);
        return used == text.size() && value <= 0xFFFFu ? static_cast<std::uint16_t>(value) : fallback;
    } catch (...) {
        return fallback;
    }
}

std::wstring CMainFrame::ControlText(const CWnd& control) const {
    CString value;
    if (control.GetSafeHwnd() != nullptr) control.GetWindowTextW(value);
    return std::wstring(value.GetString(), static_cast<std::size_t>(value.GetLength()));
}

void CMainFrame::StartWorker(WorkKind kind,
                             std::function<std::unique_ptr<WorkerResult>()> work) {
    if (worker_running_.exchange(true)) return;
    JoinCompletedWorker();
    cancel_requested_.store(false);
    UpdateUiState();
    worker_ = std::thread([this, kind, work = std::move(work)]() mutable {
        std::unique_ptr<WorkerResult> result;
        try {
            result = work();
            if (!result) result = std::make_unique<WorkerResult>();
            result->success = true;
        } catch (const rl78_programmer::sdk::ProgrammerException& ex) {
            result = std::make_unique<WorkerResult>();
            result->error_status = ex.status();
            result->message = L"Programmer error " + rl78_programmer::sdk::ErrorName(ex.status()) +
                              L": " + NarrowToWide(ex.what());
        } catch (const std::exception& ex) {
            result = std::make_unique<WorkerResult>();
            result->message = NarrowToWide(ex.what());
        }
        result->kind = kind;
        worker_running_.store(false);
        if (!closing_.load() && PostMessageW(kWorkerDoneMessage, 0, reinterpret_cast<LPARAM>(result.get()))) {
            result.release();
        }
    });
}

void CMainFrame::JoinCompletedWorker() {
    if (worker_.joinable() && !worker_running_.load()) worker_.join();
}

void CMainFrame::PostProgress(WorkKind kind,
                              std::uint16_t basis_points,
                              const std::wstring& message,
                              std::uint64_t loaded_bytes,
                              std::uint64_t load_total_bytes,
                              std::uint64_t programmed_bytes,
                              std::uint64_t program_total_bytes) {
    auto progress = std::make_unique<WorkerProgress>();
    progress->kind = kind;
    progress->basis_points = basis_points;
    progress->loaded_bytes = loaded_bytes;
    progress->load_total_bytes = load_total_bytes;
    progress->programmed_bytes = programmed_bytes;
    progress->program_total_bytes = program_total_bytes;
    progress->message = message;
    if (!closing_.load() && PostMessageW(kWorkerProgressMessage, 0, reinterpret_cast<LPARAM>(progress.get()))) {
        progress.release();
    }
}

void CMainFrame::SetProgressDisplay(WorkKind kind,
                                    std::uint16_t basis_points,
                                    std::uint64_t loaded_bytes,
                                    std::uint64_t load_total_bytes,
                                    std::uint64_t programmed_bytes,
                                    std::uint64_t program_total_bytes) {
    const std::uint16_t position =
        (std::min)(basis_points, static_cast<std::uint16_t>(10000u));
    const std::wstring indicator = ProgressIndicatorText(
        position, loaded_bytes, load_total_bytes,
        programmed_bytes, program_total_bytes);
    if (kind == WorkKind::UploadBridgeFirmware ||
        kind == WorkKind::LoadBridgeFirmware) {
        bridge_fw_progress_.SetPos(position);
        bridge_fw_progress_text_.SetWindowTextW(indicator.c_str());
        return;
    }
    if (kind == WorkKind::ProgramOnline || kind == WorkKind::LoadImage) {
        progress_.SetPos(position);
        program_progress_text_.SetWindowTextW(indicator.c_str());
    }
    if (kind == WorkKind::Download ||
        kind == WorkKind::ProgramOffline ||
        kind == WorkKind::LoadImage) {
        offline_progress_.SetPos(position);
        offline_progress_text_.SetWindowTextW(indicator.c_str());
    }
}

void CMainFrame::SetBridgeWorkflowState(BridgeWorkflowStep step,
                                        WorkflowState state) {
    const std::size_t index = static_cast<std::size_t>(step);
    if (index >= bridge_fw_step_states_.size()) return;
    bridge_fw_step_states_[index] = state;
    if (bridge_fw_steps_[index].GetSafeHwnd() == nullptr) return;
    const wchar_t* state_text = L"Pending";
    switch (state) {
    case WorkflowState::InProgress:
        state_text = L"In progress...";
        break;
    case WorkflowState::Ready:
        state_text = L"Ready for user action";
        break;
    case WorkflowState::Completed:
        state_text = L"Completed";
        break;
    case WorkflowState::Failed:
        state_text = L"Failed - retry this step";
        break;
    case WorkflowState::Pending:
    default:
        break;
    }
    std::wstring text = L"  ";
    text.append(kBridgeWorkflowLabels[index].data(),
                kBridgeWorkflowLabels[index].size());
    text += L"    |    ";
    text += state_text;
    bridge_fw_steps_[index].SetWindowTextW(text.c_str());
    bridge_fw_steps_[index].Invalidate();
}

void CMainFrame::SetBridgeWorkflowFailure(WorkKind kind) {
    switch (kind) {
    case WorkKind::LoadBridgeFirmware:
        SetBridgeWorkflowState(BridgeWorkflowStep::LoadAndValidate,
                               WorkflowState::Failed);
        break;
    case WorkKind::ReturnBootloader:
        SetBridgeWorkflowState(BridgeWorkflowStep::ReturnBootloader,
                               WorkflowState::Failed);
        break;
    case WorkKind::ScanBridgeBootloader:
        SetBridgeWorkflowState(BridgeWorkflowStep::ScanBootloader,
                               WorkflowState::Failed);
        break;
    case WorkKind::UploadBridgeFirmware:
        SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                               WorkflowState::Failed);
        break;
    case WorkKind::RunBridgeApplication:
        SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                               WorkflowState::Failed);
        break;
    default:
        break;
    }
}

void CMainFrame::SetOfflineWorkflowState(OfflineWorkflowStep step,
                                         WorkflowState state) {
    const std::size_t index = static_cast<std::size_t>(step);
    if (index >= offline_step_states_.size()) return;
    offline_step_states_[index] = state;
    if (offline_steps_[index].GetSafeHwnd() == nullptr) return;
    const wchar_t* state_text = L"Pending";
    switch (state) {
    case WorkflowState::InProgress:
        state_text = L"In progress...";
        break;
    case WorkflowState::Ready:
        state_text = L"Ready for user action";
        break;
    case WorkflowState::Completed:
        state_text = L"Completed";
        break;
    case WorkflowState::Failed:
        state_text = L"Failed - retry this step";
        break;
    case WorkflowState::Pending:
    default:
        break;
    }
    std::wstring label(kOfflineWorkflowLabels[index]);
    if (index ==
            static_cast<std::size_t>(
                OfflineWorkflowStep::DownloadToSpiFlash) &&
        IsUartBoardSelected()) {
        label = L"Step 2  Download to Board Storage (M032 internal APROM)";
    } else if (index ==
                   static_cast<std::size_t>(
                       OfflineWorkflowStep::TriggerProgramming) &&
               IsUartBoardSelected()) {
        label = L"Step 5  Trigger on board (PC.2 button)";
    }
    std::wstring text = L"  ";
    text += label;
    text += L"    |    ";
    text += state_text;
    offline_steps_[index].SetWindowTextW(text.c_str());
    offline_steps_[index].Invalidate();
}

void CMainFrame::ResetOfflineWorkflowAfter(OfflineWorkflowStep step) {
    const std::size_t first = static_cast<std::size_t>(step) + 1u;
    for (std::size_t i = first; i < offline_step_states_.size(); ++i) {
        SetOfflineWorkflowState(static_cast<OfflineWorkflowStep>(i),
                                WorkflowState::Pending);
    }
}

void CMainFrame::SetOfflineWorkflowFailure(WorkKind kind) {
    switch (kind) {
    case WorkKind::LoadImage:
        SetOfflineWorkflowState(OfflineWorkflowStep::LoadAndValidate,
                                WorkflowState::Failed);
        break;
    case WorkKind::Download:
        SetOfflineWorkflowState(OfflineWorkflowStep::DownloadToSpiFlash,
                                WorkflowState::Failed);
        break;
    case WorkKind::Activate:
        SetOfflineWorkflowState(OfflineWorkflowStep::ActivatePackage,
                                WorkflowState::Failed);
        break;
    case WorkKind::ArmOffline:
        SetOfflineWorkflowState(OfflineWorkflowStep::ArmOffline,
                                WorkflowState::Failed);
        break;
    case WorkKind::ProgramOffline:
        SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                                WorkflowState::Failed);
        break;
    default:
        break;
    }
}

void CMainFrame::OnScan() {
    const auto vid = ReadHexControl(vid_edit_, 0x0416u);
    const auto pid = ReadHexControl(pid_edit_, 0x5020u);
    const bool uart = IsUartBoardSelected();
    SetStatus(L"Scanning...");
    StartWorker(WorkKind::Scan, [vid, pid, uart]() {
        auto result = std::make_unique<WorkerResult>();
        result->devices = uart
            ? rl78_programmer::sdk::ProgrammerClient::EnumerateSerial()
            : rl78_programmer::sdk::ProgrammerClient::Enumerate(
                  vid, pid, true);
        result->message =
            L"Found " + std::to_wstring(result->devices.size()) +
            (uart ? L" COM port(s)." : L" matching HID device(s).");
        return result;
    });
}

void CMainFrame::OnConnect() {
    const int index = device_combo_.GetCurSel();
    if (index < 0 || static_cast<std::size_t>(index) >= devices_.size()) return;
    const auto descriptor = devices_[static_cast<std::size_t>(index)];
    const int timeout = config_.timeout_ms;
    const auto board_profile = SelectedBoardProfile();
    SetStatus(L"Connecting...");
    StartWorker(WorkKind::Connect,
                [this, descriptor, timeout, board_profile]() {
        auto result = std::make_unique<WorkerResult>();
        client_.Connect(descriptor, timeout);
        const auto uptime = client_.Ping();
        result->programmer_info = client_.GetDeviceInfo();
        if (board_profile == ProgrammerBoardProfile::M032TestBoardUart &&
            result->programmer_info->product_name !=
                L"M032 TEST BOARD") {
            client_.Disconnect();
            throw rl78_programmer::sdk::ProgrammerException(
                PLP_ERR_UPDATE_INCOMPATIBLE,
                "the selected COM port did not report the M032 TEST BOARD identity");
        }
        result->operating_mode = client_.GetOperatingMode();
        try {
            result->offline_result = client_.GetLastOfflineResult();
        } catch (const rl78_programmer::sdk::ProgrammerException& error) {
            if (error.status() != PLP_ERR_COMMAND_UNSUPPORTED) throw;
        }
        try {
            result->offline_failure_history =
                client_.GetOfflineFailureHistory();
        } catch (const rl78_programmer::sdk::ProgrammerException& error) {
            if (error.status() != PLP_ERR_COMMAND_UNSUPPORTED) throw;
        }
        result->capabilities = client_.Capabilities();
        result->message =
            L"Connected; product=" +
            result->programmer_info->product_name +
            L"; firmware=" +
            result->programmer_info->firmware_version +
            L"; uptime=" + std::to_wstring(uptime) + L" ms.";
        return result;
    });
}

void CMainFrame::OnDisconnect() {
    client_.Disconnect();
    connected_offline_button_ = false;
    oip_download_.SetWindowTextW(L"Download to Board Storage");
    downloaded_package_uuid_.fill(0);
    SetStatus(L"Disconnected");
    device_info_.SetWindowTextW(L"No programmer connected");
    operating_mode_ = {};
    active_package_uuid_.fill(0);
    UpdateOperatingModeText();
    AppendLog(L"Programmer disconnected.");
    UpdateUiState();
}

void CMainFrame::OnResetMcu() {
    if (!client_.IsConnected()) return;
    const bool uart = IsUartBoardSelected();
    SetStatus(L"Resetting bridge MCU...");
    AppendLog(L"event=PROGRAMMER_RESET_REQUESTED");
    StartWorker(WorkKind::ResetMcu, [this, uart]() {
        auto result = std::make_unique<WorkerResult>();
        client_.ResetProgrammer();
        result->message =
            uart
                ? L"Bridge MCU reset acknowledged. Scan and reconnect after the UART application restarts."
                : L"Bridge MCU reset acknowledged. Scan and reconnect after USB re-enumeration.";
        return result;
    });
}

void CMainFrame::OnBrowseMot() {
    CFileDialog dialog(TRUE, L"mot", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                       L"RL78 firmware (*.mot;*.srec;*.s19;*.hex;*.bin)|*.mot;*.srec;*.s19;*.hex;*.bin|Motorola S-record / Intel HEX (*.mot;*.srec;*.s19;*.hex)|*.mot;*.srec;*.s19;*.hex|Raw binary (*.bin)|*.bin|All files (*.*)|*.*||",
                       this);
    if (dialog.DoModal() == IDOK) {
        mot_path_.SetWindowTextW(dialog.GetPathName());
        validated_image_source_.Clear();
        package_.reset();
        image_.reset();
        downloaded_package_uuid_.fill(0);
        SetProgressDisplay(WorkKind::LoadImage, 0);
        SetOfflineWorkflowState(OfflineWorkflowStep::LoadAndValidate,
                                WorkflowState::Pending);
        ResetOfflineWorkflowAfter(OfflineWorkflowStep::LoadAndValidate);
        image_summary_.SetWindowTextW(
            L"File selected; Load + Validate is required before programming.");
        program_status_.SetWindowTextW(
            L"Next step: click Load + Validate.");
        offline_status_.SetWindowTextW(
            L"Step 1: click Load + Validate. The OIP will be generated automatically; no separate OIP input file is required.");
        UpdateUiState();
    }
}

void CMainFrame::OnMotPathChanged() {
    const std::wstring path = ControlText(mot_path_);
    if (validated_image_source_.Matches(path)) return;
    validated_image_source_.Clear();
    package_.reset();
    image_.reset();
    downloaded_package_uuid_.fill(0);
    SetProgressDisplay(WorkKind::LoadImage, 0);
    SetOfflineWorkflowState(OfflineWorkflowStep::LoadAndValidate,
                            WorkflowState::Pending);
    ResetOfflineWorkflowAfter(OfflineWorkflowStep::LoadAndValidate);
    image_summary_.SetWindowTextW(
        path.empty()
            ? L"No firmware file selected."
            : L"Firmware path changed; Load + Validate is required before programming.");
    program_status_.SetWindowTextW(
        path.empty() ? L"Select a firmware file." :
                       L"Next step: click Load + Validate.");
    offline_status_.SetWindowTextW(
        path.empty()
            ? L"Step 1: select a firmware file."
            : L"Step 1: click Load + Validate. A previously validated image cannot be reused after its path changes.");
    UpdateUiState();
}

void CMainFrame::OnLoadMot() {
    const auto path = ControlText(mot_path_);
    const auto target_profile = SelectedTargetProfile();
    if (path.empty()) return;
    validated_image_source_.Clear();
    package_.reset();
    image_.reset();
    downloaded_package_uuid_.fill(0);
    SetProgressDisplay(WorkKind::LoadImage, 0);
    SetOfflineWorkflowState(OfflineWorkflowStep::LoadAndValidate,
                            WorkflowState::InProgress);
    ResetOfflineWorkflowAfter(OfflineWorkflowStep::LoadAndValidate);
    program_status_.SetWindowTextW(L"Parsing and validating image...");
    offline_status_.SetWindowTextW(
        L"Step 1 in progress: parsing the firmware and generating a validated OIP in memory...");
    UpdateUiState();
    StartWorker(WorkKind::LoadImage, [path, target_profile]() {
        auto result = std::make_unique<WorkerResult>();
        result->image_source_path = path;
        auto source = ReadFileBytes(path);
        result->load_total_bytes = source.size();
        std::array<std::uint8_t, 32> source_hash{};
        Rpp_Sha256(source.data(), source.size(), source_hash.data());
        if (source.empty()) throw std::runtime_error("firmware image is empty");
        auto extension = std::filesystem::path(path).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
        rl78_programmer::SRecordImage image;
        std::wstring source_format = L"raw binary";
        bool extension_mismatch = false;
        if (extension == L".bin") {
            if (source.size() > static_cast<std::size_t>(UINT32_MAX)) {
                throw std::runtime_error("raw binary image is too large");
            }
            rl78_programmer::ImageSegment segment;
            segment.address = 0u;
            segment.data = std::move(source);
            image.segments.push_back(std::move(segment));
            image.source_record_count = 1u;
        } else {
            const std::string_view source_text(
                reinterpret_cast<const char*>(source.data()), source.size());
            const auto format = rl78_programmer::DetectAddressedTextImageFormat(source_text);
            source_format = NarrowToWide(rl78_programmer::AddressedTextImageFormatName(format));
            extension_mismatch = extension == L".mot" &&
                format == rl78_programmer::AddressedTextImageFormat::IntelHex;
            image = rl78_programmer::ParseAddressedTextImage(source_text);
        }
        rl78_programmer::OipBuildOptions options;
        options.target_profile = target_profile;
        options.project_name = target_profile.display_name + " Project";
        options.source_name = std::filesystem::path(path).filename().u8string();
        options.source_sha256 = source_hash;
        auto package = rl78_programmer::BuildOfflineImagePackage(image, options);
        result->message = (extension_mismatch
                               ? L"Notice: .mot file contains Intel HEX content. "
                               : L"Detected " + source_format + L". ") +
                           L"Validated " + std::to_wstring(image.DataSize()) +
                           L" bytes in " + std::to_wstring(image.segments.size()) + L" source segment(s).";
        result->program_total_bytes = image.DataSize();
        result->image = std::move(image);
        result->package = std::move(package);
        return result;
    });
}

void CMainFrame::OnTargetProfileChanged() {
    validated_image_source_.Clear();
    package_.reset();
    image_.reset();
    downloaded_package_uuid_.fill(0);
    SetOfflineWorkflowState(OfflineWorkflowStep::LoadAndValidate,
                            WorkflowState::Pending);
    ResetOfflineWorkflowAfter(OfflineWorkflowStep::LoadAndValidate);
    image_summary_.SetWindowTextW(
        L"Target profile changed; load and validate the firmware file again.");
    program_status_.SetWindowTextW(L"Next step: click Load + Validate.");
    offline_status_.SetWindowTextW(
        L"Target profile changed. Step 1 must be completed again before the package can be downloaded.");
    SetProgressDisplay(WorkKind::LoadImage, 0);
    UpdateUiState();
}

void CMainFrame::OnReadSignature() {
    const auto profile_uuid = SelectedTargetProfile().uuid;
    StartWorker(WorkKind::ReadSignature, [this, profile_uuid]() {
        auto result = std::make_unique<WorkerResult>();
        result->signature = client_.ReadTargetSignature(profile_uuid);
        result->message = L"Target signature digest: " +
                          HexDigest(result->signature->signature_digest.data(),
                                    result->signature->signature_digest.size());
        return result;
    });
}

void CMainFrame::OnProgramOnline() {
    if (!package_ ||
        !validated_image_source_.Matches(ControlText(mot_path_))) {
        program_status_.SetWindowTextW(
            L"Firmware path changed. Click Load + Validate before Program Online.");
        UpdateUiState();
        return;
    }
    const auto& profile = SelectedTargetProfile();
    AppendLog(L"event=JOB_START mode=Online profile=" +
              NarrowToWide(profile.display_name.c_str()) +
              L" source=" + ControlText(mot_path_) +
              L" package=" +
              rl78_programmer::sdk::FormatUuid(package_->package_uuid));
    downloaded_package_uuid_.fill(0);
    active_package_uuid_.fill(0);
    operating_mode_.active_package_available = false;
    operating_mode_.active_package_uuid.fill(0);
    UpdateOperatingModeText();
    ResetOfflineWorkflowAfter(OfflineWorkflowStep::LoadAndValidate);
    SetProgressDisplay(WorkKind::ProgramOnline, 0, 0,
                       package_->bytes.size(), 0,
                       image_ ? image_->DataSize() : 0u);
    StartWorker(WorkKind::ProgramOnline, [this]() { return RunProgram(WorkKind::ProgramOnline, 0); });
}

void CMainFrame::OnCancelJob() {
    cancel_requested_.store(true);
    program_status_.SetWindowTextW(L"Cancellation requested; waiting for target safe point...");
    AppendLog(L"Cancellation requested.");
}

void CMainFrame::OnSaveOip() {
    if (!package_) return;
    std::wstring default_name = std::filesystem::path(ControlText(mot_path_)).stem().wstring();
    if (default_name.empty()) default_name = L"rl78_offline";
    default_name += L".oip";
    CFileDialog dialog(FALSE, L"oip", default_name.c_str(),
                       OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY,
                       L"Offline Image Package (*.oip)|*.oip||", this);
    if (dialog.DoModal() == IDOK) {
        try {
            rl78_programmer::SaveOfflineImagePackage(*package_, dialog.GetPathName().GetString());
            AppendLog(L"Saved OIP: " + std::wstring(dialog.GetPathName()));
        } catch (const std::exception& ex) {
            MessageBoxW(NarrowToWide(ex.what()).c_str(),
                        L"Save OIP", MB_OK | MB_ICONERROR);
        }
    }
}

void CMainFrame::OnDownloadOip() {
    if (!package_) return;
    downloaded_package_uuid_.fill(0);
    active_package_uuid_.fill(0);
    operating_mode_.active_package_available = false;
    operating_mode_.active_package_uuid.fill(0);
    UpdateOperatingModeText();
    const auto package = *package_;
    const std::uint64_t package_size = package.bytes.size();
    const std::wstring storage_name =
        IsUartBoardSelected()
            ? L"M032 internal APROM"
            : (SelectedBoardProfile() ==
                       ProgrammerBoardProfile::Nulink2MeUsb
                   ? L"SPI Flash"
                   : L"package storage");
    SetProgressDisplay(WorkKind::Download, 0, 0, package_size, 0, 0);
    SetOfflineWorkflowState(OfflineWorkflowStep::DownloadToSpiFlash,
                            WorkflowState::InProgress);
    ResetOfflineWorkflowAfter(OfflineWorkflowStep::DownloadToSpiFlash);
    offline_status_.SetWindowTextW(
        (L"Step 2 in progress: erasing/programming the package slot in " +
         storage_name + L"...").c_str());
    StartWorker(WorkKind::Download, [this, package, package_size, storage_name]() {
        auto result = std::make_unique<WorkerResult>();
        result->load_total_bytes = package_size;
        client_.DownloadPackage(package, [this, package_size]
                                (std::uint16_t value, const std::wstring& text) {
            const std::uint64_t loaded_bytes = (package_size * value) / 10000u;
            PostProgress(WorkKind::Download, value, text,
                         loaded_bytes, package_size, 0, 0);
        });
        result->message = L"Step 2 complete: package downloaded to " +
                          storage_name +
                          L" and validated. Next step: click Activate Package.";
        return result;
    });
}

void CMainFrame::OnActivateOip() {
    if (!package_ || downloaded_package_uuid_ != package_->package_uuid) return;
    const auto package_uuid = package_->package_uuid;
    SetOfflineWorkflowState(OfflineWorkflowStep::ActivatePackage,
                            WorkflowState::InProgress);
    ResetOfflineWorkflowAfter(OfflineWorkflowStep::ActivatePackage);
    offline_status_.SetWindowTextW(
        L"Step 3 in progress: making the downloaded package the active offline source...");
    StartWorker(WorkKind::Activate, [this, package_uuid]() {
        auto result = std::make_unique<WorkerResult>();
        client_.ActivatePackage(package_uuid);
        result->message = L"Step 3 complete: offline package activated: " +
                          rl78_programmer::sdk::FormatUuid(package_uuid) +
                          L". Next step: click Arm Offline.";
        return result;
    });
}

void CMainFrame::OnProgramOffline() {
    if (!package_ || IsZeroUuid(active_package_uuid_) ||
        active_package_uuid_ != package_->package_uuid) return;
    AppendLog(L"event=JOB_START mode=Offline package=" +
              rl78_programmer::sdk::FormatUuid(active_package_uuid_));
    SetProgressDisplay(WorkKind::ProgramOffline, 0);
    SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                            WorkflowState::InProgress);
    offline_status_.SetWindowTextW(
        IsUartBoardSelected()
            ? L"Testing the active internal-APROM package through the connected UART session..."
            : (SelectedBoardProfile() ==
                       ProgrammerBoardProfile::Nulink2MeUsb
                   ? L"Testing the active SPI Flash package through the connected HID session..."
                   : L"Testing the active stored package through the connected HID session..."));
    StartWorker(WorkKind::ProgramOffline, [this]() { return RunProgram(WorkKind::ProgramOffline, 1); });
}

void CMainFrame::OnArmOffline() {
    const auto generation = operating_mode_.generation;
    const bool offline_button = connected_offline_button_;
    const auto board_profile = SelectedBoardProfile();
    SetOfflineWorkflowState(OfflineWorkflowStep::ArmOffline,
                            WorkflowState::InProgress);
    SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                            WorkflowState::Pending);
    offline_status_.SetWindowTextW(L"Step 4 in progress: arming Offline mode...");
    StartWorker(WorkKind::ArmOffline,
                [this, generation, offline_button, board_profile]() {
        auto result = std::make_unique<WorkerResult>();
        result->operating_mode = client_.SetOperatingMode(
            PLP_OPERATING_MODE_OFFLINE_ARMED, true, generation);
        if (board_profile ==
            ProgrammerBoardProfile::M032TestBoardUart) {
            result->message =
                L"Bridge is OFFLINE ARMED. The VCOM may be disconnected; keep 3.3 V power and GND connected, then press PC.2.";
        } else if (offline_button) {
            result->message =
                L"Bridge is OFFLINE ARMED. Disconnect the GUI session, keep USB power connected, then press the programmer's physical offline button (Nu-Link2-Me KEY1).";
        } else {
            result->message =
                L"Bridge is OFFLINE ARMED. Press M032 Reset to start; USB HID remains available.";
        }
        return result;
    });
}

void CMainFrame::OnSwitchOnline() {
    const auto generation = operating_mode_.generation;
    SetOfflineWorkflowState(OfflineWorkflowStep::ArmOffline,
                            WorkflowState::Pending);
    SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                            WorkflowState::Pending);
    StartWorker(WorkKind::SwitchOnline, [this, generation]() {
        auto result = std::make_unique<WorkerResult>();
        result->operating_mode = client_.SetOperatingMode(
            PLP_OPERATING_MODE_ONLINE_ONLY, false, generation);
        result->message = L"Bridge switched to ONLINE ONLY; stored package was preserved.";
        return result;
    });
}

void CMainFrame::OnBrowseBridgeFirmware() {
    CFileDialog dialog(TRUE, L"bin", nullptr,
                       OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                       L"Bridge application binary (*.bin)|*.bin|All files (*.*)|*.*||",
                       this);
    if (dialog.DoModal() == IDOK) {
        bridge_fw_path_.SetWindowTextW(dialog.GetPathName());
        bridge_firmware_image_.reset();
        SetBridgeWorkflowState(BridgeWorkflowStep::LoadAndValidate,
                               WorkflowState::Pending);
        SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                               WorkflowState::Pending);
        SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                               WorkflowState::Pending);
        SetProgressDisplay(WorkKind::LoadBridgeFirmware, 0);
        bridge_fw_status_.SetWindowTextW(
            L"File selected; Load + Validate is required before upload.");
        UpdateUiState();
    }
}

void CMainFrame::OnBridgeFirmwareProfileChanged() {
    bridge_firmware_image_.reset();
    SetBridgeWorkflowState(BridgeWorkflowStep::LoadAndValidate,
                           WorkflowState::Pending);
    SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                           WorkflowState::Pending);
    SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                           WorkflowState::Pending);
    SetProgressDisplay(WorkKind::LoadBridgeFirmware, 0);
    bridge_fw_status_.SetWindowTextW(
        L"Bridge profile changed; load and validate the application BIN again.");
    UpdateUiState();
}

void CMainFrame::OnLoadBridgeFirmware() {
    const auto path = ControlText(bridge_fw_path_);
    const auto target = SelectedBridgeFirmwareTarget();
    if (path.empty()) return;
    bridge_firmware_image_.reset();
    SetBridgeWorkflowState(BridgeWorkflowStep::LoadAndValidate,
                           WorkflowState::InProgress);
    SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                           WorkflowState::Pending);
    SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                           WorkflowState::Pending);
    SetProgressDisplay(WorkKind::LoadBridgeFirmware, 0);
    bridge_fw_status_.SetWindowTextW(L"Validating bridge application image...");
    StartWorker(WorkKind::LoadBridgeFirmware, [path, target]() {
        auto result = std::make_unique<WorkerResult>();
        result->bridge_firmware_image =
            rl78_programmer::sdk::BridgeFirmwareUpdater::LoadApplicationImage(target, path);
        const auto& image = *result->bridge_firmware_image;
        result->load_total_bytes = image.bytes.size();
        result->program_total_bytes = image.bytes.size();
        result->message = L"Bridge image validated: " + image.profile.display_name +
                          L", " + std::to_wstring(image.bytes.size()) +
                          L" bytes, CRC32 valid.";
        return result;
    });
}

void CMainFrame::OnReturnBootloader() {
    if (!client_.IsConnected()) return;
    const bool uart = IsUartBoardSelected();
    const std::wstring prompt =
        L"This erases the bridge application checksum and reboots the board into the " +
        std::wstring(uart ? L"UART ISP bootloader.\r\n\r\n"
                          : L"USB HID bootloader.\r\n\r\n") +
        L"If upload is interrupted, the board intentionally remains in bootloader mode until a valid bridge application is uploaded. Continue?";
    const int choice = MessageBoxW(
        prompt.c_str(),
        L"Return Boot loader", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (choice != IDYES) return;
    bridge_firmware_updater_.Disconnect();
    bootloader_devices_.clear();
    bridge_fw_device_combo_.ResetContent();
    SetBridgeWorkflowState(BridgeWorkflowStep::ReturnBootloader,
                           WorkflowState::InProgress);
    SetBridgeWorkflowState(BridgeWorkflowStep::ScanBootloader,
                           WorkflowState::Pending);
    SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                           WorkflowState::Pending);
    SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                           WorkflowState::Pending);
    bridge_fw_status_.SetWindowTextW(L"Requesting bootloader entry...");
    StartWorker(WorkKind::ReturnBootloader, [this, uart]() {
        auto result = std::make_unique<WorkerResult>();
        try {
            client_.EnterBootloader();
        } catch (...) {
            client_.Disconnect();
            throw;
        }
        client_.Disconnect();
        result->message = uart
            ? L"Bridge acknowledged Return Boot loader and released the COM port. Wait for reset, then scan the external VCOM port."
            : L"Bridge acknowledged Return Boot loader. Scan for USB HID ISP PID 0x3F00.";
        return result;
    });
}

void CMainFrame::OnScanBridgeBootloader() {
    bridge_firmware_updater_.Disconnect();
    bootloader_devices_.clear();
    bridge_fw_device_combo_.ResetContent();
    SetBridgeWorkflowState(BridgeWorkflowStep::ScanBootloader,
                           WorkflowState::InProgress);
    SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                           WorkflowState::Pending);
    SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                           WorkflowState::Pending);
    const bool uart = IsUartBoardSelected();
    bridge_fw_status_.SetWindowTextW(
        uart ? L"Scanning COM ports and verifying M032 TEST BOARD UART ISP identity..."
             : L"Scanning for Nuvoton USB HID bootloader...");
    const int timeout = config_.timeout_ms;
    StartWorker(WorkKind::ScanBridgeBootloader, [this, uart, timeout]() {
        auto result = std::make_unique<WorkerResult>();
        if (!uart) {
            result->bootloader_devices =
                rl78_programmer::sdk::BridgeFirmwareUpdater::
                    EnumerateBootloaders();
            result->message =
                L"Found " +
                std::to_wstring(result->bootloader_devices.size()) +
                L" USB HID bootloader device(s).";
            return result;
        }

        const auto ports =
            rl78_programmer::sdk::BridgeFirmwareUpdater::
                EnumerateSerialPorts();
        std::wostringstream diagnostics;
        bool first_diagnostic = true;
        const int probe_timeout = (std::min)(
            (std::max)(timeout, 500), 2000);
        for (const auto& port : ports) {
            if (cancel_requested_.load()) {
                throw rl78_programmer::sdk::ProgrammerException(
                    PLP_ERR_SDK_BUSY,
                    "UART ISP scan cancelled by user");
            }
            try {
                rl78_programmer::sdk::BridgeFirmwareUpdater probe;
                probe.Connect(port, probe_timeout);
                const auto info = probe.Probe(
                    [this]() { return cancel_requested_.load(); },
                    probe_timeout);
                rl78_programmer::sdk::BridgeFirmwareUpdater::
                    ValidateCompatibility(
                        rl78_programmer::sdk::BridgeFirmwareUpdater::
                            Profile(
                                rl78_programmer::sdk::
                                    BridgeFirmwareTarget::
                                        M032TestBoard),
                        info.application_geometry);
                result->bootloader_devices.push_back(port);
            } catch (const rl78_programmer::sdk::
                         ProgrammerException& error) {
                if (!first_diagnostic) {
                    diagnostics << L"; ";
                }
                first_diagnostic = false;
                diagnostics
                    << (port.serial_number.empty()
                            ? port.path
                            : port.serial_number)
                    << L"=" <<
                    rl78_programmer::sdk::ErrorName(error.status());
            }
        }
        result->message =
            L"Scanned " + std::to_wstring(ports.size()) +
            L" COM port(s); verified " +
            std::to_wstring(result->bootloader_devices.size()) +
            L" M032 TEST BOARD UART ISP bootloader(s) by MTB1 identity.";
        if (result->bootloader_devices.empty()) {
            result->message +=
                L"\r\nNo MTB1 bootloader responded. Connect to the "
                L"Application first and click Return Boot loader, or hold "
                L"PC.2 while resetting/powering the M032, then scan again.";
        }
        if (!first_diagnostic) {
            result->message += L"\r\nProbe result: " +
                               diagnostics.str();
        }
        return result;
    });
}

void CMainFrame::OnUploadBridgeFirmware() {
    const int index = bridge_fw_device_combo_.GetCurSel();
    if (!bridge_firmware_image_ || index < 0 ||
        static_cast<std::size_t>(index) >= bootloader_devices_.size()) return;
    const auto descriptor = bootloader_devices_[static_cast<std::size_t>(index)];
    const auto image = *bridge_firmware_image_;
    const int timeout = config_.timeout_ms;
    SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                           WorkflowState::InProgress);
    SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                           WorkflowState::Pending);
    SetProgressDisplay(WorkKind::UploadBridgeFirmware, 0,
                       image.bytes.size(), image.bytes.size(),
                       0, image.bytes.size());
    bridge_fw_status_.SetWindowTextW(
        descriptor.transport == rl78_programmer::sdk::TransportKind::Uart
            ? L"Connecting to UART ISP bootloader..."
            : L"Connecting to USB HID bootloader...");
    StartWorker(WorkKind::UploadBridgeFirmware,
                [this, descriptor, image, timeout]() {
        auto result = std::make_unique<WorkerResult>();
        bridge_firmware_updater_.Disconnect();
        bridge_firmware_updater_.Connect(descriptor, timeout);
        result->bootloader_info = bridge_firmware_updater_.Probe(
            [this]() { return cancel_requested_.load(); });
        bridge_firmware_updater_.ProgramApplication(
            image,
            [this](const rl78_programmer::sdk::BridgeFirmwareUpdateProgress& value) {
                PostProgress(WorkKind::UploadBridgeFirmware,
                             value.basis_points,
                             L"Uploading bridge application",
                             value.total_bytes,
                             value.total_bytes,
                             value.bytes_done,
                             value.total_bytes);
            },
            [this]() { return cancel_requested_.load(); });
        result->message =
            L"Bridge application upload and whole-image checksum verification completed. "
            L"Select Run Application to pass the bootloader CRC gate.";
        return result;
    });
}

void CMainFrame::OnRunBridgeApplication() {
    const int index = bridge_fw_device_combo_.GetCurSel();
    if (index < 0 || static_cast<std::size_t>(index) >= bootloader_devices_.size()) return;
    const auto descriptor = bootloader_devices_[static_cast<std::size_t>(index)];
    const int timeout = config_.timeout_ms;
    SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                           WorkflowState::InProgress);
    bridge_fw_status_.SetWindowTextW(L"Starting bridge application...");
    StartWorker(WorkKind::RunBridgeApplication, [this, descriptor, timeout]() {
        auto result = std::make_unique<WorkerResult>();
        if (!bridge_firmware_updater_.IsConnected() ||
            bridge_firmware_updater_.CurrentDevice().path != descriptor.path) {
            bridge_firmware_updater_.Disconnect();
            bridge_firmware_updater_.Connect(descriptor, timeout);
            static_cast<void>(bridge_firmware_updater_.Probe(
                [this]() { return cancel_requested_.load(); }));
        }
        bridge_firmware_updater_.RunApplication();
        bridge_firmware_updater_.Disconnect();
        result->message =
            descriptor.transport == rl78_programmer::sdk::TransportKind::Uart
                ? L"Run Application sent. Wait for the UART application to restart, then Scan and Connect on the same COM port."
                : L"Run Application sent. The valid bridge application should enumerate as PID 0x5020.";
        return result;
    });
}

void CMainFrame::OnCancelBridgeFirmware() {
    cancel_requested_.store(true);
    bridge_fw_status_.SetWindowTextW(
        L"Cancellation requested. The board may remain in bootloader mode; retry upload.");
    AppendLog(L"Bridge firmware upload cancellation requested.");
}

std::unique_ptr<CMainFrame::WorkerResult> CMainFrame::RunProgram(WorkKind kind,
                                                                std::uint8_t source_kind) {
    auto result = std::make_unique<WorkerResult>();
    result->kind = kind;
    std::array<std::uint8_t, 16> source{};
    std::uint64_t load_total_bytes = 0;
    std::uint64_t program_total_bytes = image_ ? image_->DataSize() : 0u;
    if (source_kind == 0) {
        if (!package_) throw std::runtime_error("no validated package");
        load_total_bytes = package_->bytes.size();
        client_.DownloadPackage(*package_,
                                [this, kind, load_total_bytes, program_total_bytes]
                                (std::uint16_t value, const std::wstring& text) {
            const std::uint64_t loaded_bytes =
                (load_total_bytes * value) / 10000u;
            PostProgress(kind,
                         static_cast<std::uint16_t>(value / 4u),
                         text,
                         loaded_bytes,
                         load_total_bytes,
                         0,
                         program_total_bytes);
        });
        source = package_->package_uuid;
    } else {
        source = active_package_uuid_;
    }
    std::array<std::uint8_t, 16> profile_uuid{};
    if (source_kind == 0 && package_) profile_uuid = package_->target_profile_uuid;
    const auto job = client_.StartProgram(source_kind, source, profile_uuid);
    active_job_uuid_ = job;
    bool cancel_sent = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    for (;;) {
        if (cancel_requested_.load() && !cancel_sent) {
            static_cast<void>(client_.CancelJob(job));
            cancel_sent = true;
        }
        auto status = client_.QueryJob(job);
        const std::uint16_t overall_basis_points = source_kind == 0 ?
            static_cast<std::uint16_t>(2500u +
                (static_cast<std::uint32_t>(status.progress_basis_points) * 3u) / 4u) :
            status.progress_basis_points;
        const std::uint64_t reported_total = status.total != 0u ?
            status.total : program_total_bytes;
        PostProgress(kind,
                     overall_basis_points,
                     L"Programming stage: " +
                         rl78_programmer::sdk::StageName(status.stage),
                     load_total_bytes,
                     load_total_bytes,
                     status.current,
                     reported_total);
        if (status.operation_state >= PLP_OPERATION_COMPLETED) {
            result->job = status;
            if (status.terminal_result == PLP_TERMINAL_PASS) {
                result->message = L"Programming PASS";
            } else {
                result->message =
                    L"Programming FAIL | Stage " +
                    rl78_programmer::sdk::StageName(status.stage) +
                    L" (" + std::to_wstring(status.stage) +
                    L") | Error " +
                    rl78_programmer::sdk::ErrorName(status.error) +
                    L" (" + HexValue(status.error, 8) +
                    L") | Detail " + HexDetail(status.detail) +
                    L" | Attempt " + std::to_wstring(status.attempt);
            }
            active_job_uuid_.fill(0);
            return result;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw rl78_programmer::sdk::ProgrammerException(PLP_ERR_OUTCOME_UNKNOWN,
                                                             "job polling deadline expired");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

rl78_programmer::TargetProfile CMainFrame::SelectedTargetProfile() const {
    switch (target_profile_combo_.GetCurSel()) {
    case 0:
        return rl78_programmer::Rl78G15_4KTargetProfile();
    case 2:
        return rl78_programmer::Rl78G16_16KTargetProfile();
    case 3:
        return rl78_programmer::Rl78G16_32KTargetProfile();
    case 1:
    default:
        return rl78_programmer::Rl78G15_8KTargetProfile();
    }
}

rl78_programmer::sdk::BridgeFirmwareTarget
CMainFrame::SelectedBridgeFirmwareTarget() const {
    switch (bridge_fw_profile_combo_.GetCurSel()) {
    case 1:
        return rl78_programmer::sdk::BridgeFirmwareTarget::M484SIDAE;
    case 2:
        return rl78_programmer::sdk::BridgeFirmwareTarget::M032TestBoard;
    case 0:
    default:
        return rl78_programmer::sdk::BridgeFirmwareTarget::M032SE3AE;
    }
}

void CMainFrame::OnClearHistory() {
    history_.DeleteAllItems();
}

void CMainFrame::OnClearLog() {
    log_edit_.SetWindowTextW(L"");
}

void CMainFrame::OnToggleFileLog() {
    if (log_file_enable_.GetCheck() != BST_CHECKED) {
        if (session_file_logger_.IsOpen()) {
            AppendLog(L"event=FILE_LOG_DISABLED reason=user");
            if (!session_file_logger_.IsOpen()) {
                return;
            }
            session_file_logger_.Close();
        }
        log_file_status_.SetWindowTextW(L"File logging disabled");
        PersistFileLogPreference();
        return;
    }

    try {
        const std::filesystem::path directory = ExecutableDirectory() / L"logs";
        std::wstring error;
        if (!session_file_logger_.Open(directory, LogFileBaseName(), &error)) {
            DisableFileLogAfterError(error);
            return;
        }
        const std::wstring path = session_file_logger_.Path().wstring();
        log_file_status_.SetWindowTextW((L"Writing: " + path).c_str());
        AppendLog(L"event=FILE_LOG_ENABLED schema=rl78-programmer-log/1 path=" + path);
        if (!session_file_logger_.IsOpen()) {
            return;
        }
        PersistFileLogPreference();
    } catch (const std::exception& ex) {
        DisableFileLogAfterError(NarrowToWide(ex.what()));
    }
}

LRESULT CMainFrame::OnWorkerProgress(WPARAM, LPARAM parameter) {
    std::unique_ptr<WorkerProgress> progress(reinterpret_cast<WorkerProgress*>(parameter));
    if (progress) {
        SetProgressDisplay(progress->kind,
                           progress->basis_points,
                           progress->loaded_bytes,
                           progress->load_total_bytes,
                           progress->programmed_bytes,
                           progress->program_total_bytes);
        switch (progress->kind) {
        case WorkKind::ProgramOnline:
            program_status_.SetWindowTextW(progress->message.c_str());
            break;
        case WorkKind::ProgramOffline:
            program_status_.SetWindowTextW(progress->message.c_str());
            offline_status_.SetWindowTextW(progress->message.c_str());
            break;
        case WorkKind::Download:
            offline_status_.SetWindowTextW(progress->message.c_str());
            break;
        case WorkKind::UploadBridgeFirmware:
            bridge_fw_status_.SetWindowTextW(progress->message.c_str());
            break;
        default:
            break;
        }
    }
    return 0;
}

LRESULT CMainFrame::OnWorkerDone(WPARAM, LPARAM parameter) {
    std::unique_ptr<WorkerResult> result(reinterpret_cast<WorkerResult*>(parameter));
    JoinCompletedWorker();
    if (!result) return 0;
    if (result->success && result->kind == WorkKind::LoadImage &&
        result->image_source_path != ControlText(mot_path_)) {
        result->success = false;
        result->message =
            L"Firmware path changed while validation was running. "
            L"Click Load + Validate for the currently displayed file.";
        result->package.reset();
        result->image.reset();
    }
    if (!result->success) {
        if (IsUartBoardSelected() &&
            result->error_status == PLP_ERR_UART_TIMEOUT &&
            (result->kind == WorkKind::Connect ||
             result->kind == WorkKind::ReturnBootloader ||
             result->kind == WorkKind::UploadBridgeFirmware ||
             result->kind == WorkKind::RunBridgeApplication)) {
            result->message +=
                L"\r\n\r\nM032 TEST BOARD UART check: use a 3.3 V TTL VCOM "
                L"at 115200 8-N-1, connect VCOM TX to PF.2/MCU_RX, VCOM RX "
                L"to PF.3/MCU_TX and share GND. For firmware upload, first "
                L"connect to the Application and click Return Boot loader, "
                L"or hold PC.2 during M032 reset, then Scan Bootloader again.";
        }
        SetStatus(client_.IsConnected() ? L"Connected / operation failed" : L"Disconnected");
        AppendLog(L"ERROR: " + result->message);
        SetBridgeWorkflowFailure(result->kind);
        SetOfflineWorkflowFailure(result->kind);
        if (result->kind == WorkKind::LoadBridgeFirmware ||
            result->kind == WorkKind::ReturnBootloader ||
            result->kind == WorkKind::ScanBridgeBootloader ||
            result->kind == WorkKind::UploadBridgeFirmware ||
            result->kind == WorkKind::RunBridgeApplication) {
            bridge_fw_status_.SetWindowTextW(result->message.c_str());
        } else if (result->kind == WorkKind::LoadImage) {
            program_status_.SetWindowTextW(result->message.c_str());
            offline_status_.SetWindowTextW(result->message.c_str());
        } else if (result->kind == WorkKind::ReadSignature ||
                   result->kind == WorkKind::ProgramOnline ||
                   result->kind == WorkKind::ProgramOffline) {
            program_status_.SetWindowTextW(result->message.c_str());
            if (result->kind == WorkKind::ProgramOffline) {
                offline_status_.SetWindowTextW(result->message.c_str());
            }
        } else {
            offline_status_.SetWindowTextW(result->message.c_str());
        }
        MessageBoxW(result->message.c_str(), L"RL78 Programmer", MB_OK | MB_ICONERROR);
        UpdateUiState();
        return 0;
    }
    AppendLog(result->message);
    switch (result->kind) {
    case WorkKind::Scan:
        devices_ = std::move(result->devices);
        device_combo_.ResetContent();
        for (std::size_t i = 0; i < devices_.size(); ++i) {
            const auto& device = devices_[i];
            std::wstring label =
                device.transport ==
                        rl78_programmer::sdk::TransportKind::Uart
                    ? (device.serial_number.empty() ? device.path
                                                    : device.serial_number)
                    : (device.product.empty() ? L"HID programmer"
                                              : device.product);
            if (device.transport ==
                rl78_programmer::sdk::TransportKind::Uart) {
                label += L" / M032 TEST BOARD / 115200 8-N-1";
            }
            if (device.transport !=
                    rl78_programmer::sdk::TransportKind::Uart &&
                !device.serial_number.empty()) {
                label += L" / " + device.serial_number;
            }
            device_combo_.AddString(label.c_str());
        }
        if (!devices_.empty()) device_combo_.SetCurSel(0);
        SetStatus(devices_.empty() ? L"No matching device" : L"Device found");
        break;
    case WorkKind::Connect:
        if (result->programmer_info) {
            const auto& info = *result->programmer_info;
            connected_offline_button_ =
                (result->capabilities & PLP_CAP_OFFLINE_BUTTON) != 0u;
            if (IsUartBoardSelected()) {
                oip_download_.SetWindowTextW(L"Download to Internal Flash");
            } else if (SelectedBoardProfile() ==
                       ProgrammerBoardProfile::Nulink2MeUsb) {
                oip_download_.SetWindowTextW(L"Download to SPI Flash");
            } else {
                oip_download_.SetWindowTextW(
                    L"Download to Package Storage");
            }
            const std::wstring text = info.product_name + L" | FW " + info.firmware_version +
                                      L" | UUID " + rl78_programmer::sdk::FormatUuid(info.uuid);
            device_info_.SetWindowTextW(text.c_str());
        }
        if (result->operating_mode) {
            operating_mode_ = *result->operating_mode;
            active_package_uuid_ = operating_mode_.active_package_uuid;
            UpdateOperatingModeText();
            if (package_ && operating_mode_.active_package_available &&
                active_package_uuid_ == package_->package_uuid) {
                downloaded_package_uuid_ = package_->package_uuid;
                SetOfflineWorkflowState(OfflineWorkflowStep::DownloadToSpiFlash,
                                        WorkflowState::Completed);
                SetOfflineWorkflowState(OfflineWorkflowStep::ActivatePackage,
                                        WorkflowState::Completed);
                if (operating_mode_.mode == PLP_OPERATING_MODE_OFFLINE_ARMED) {
                    SetOfflineWorkflowState(OfflineWorkflowStep::ArmOffline,
                                            WorkflowState::Completed);
                    SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                                            WorkflowState::Ready);
                }
            }
        }
        if (result->offline_result && result->offline_result->available) {
            AppendLog(OfflineResultText(*result->offline_result));
            if (result->offline_result->failure.valid &&
                !result->offline_failure_history) {
                AppendLog(OfflineFailureSnapshotText(*result->offline_result));
            }
        }
        if (result->offline_failure_history) {
            const auto& history = *result->offline_failure_history;
            std::wostringstream summary;
            summary << L"event=OFFLINE_FAILURE_HISTORY_SUMMARY"
                    << L" retained=" << history.records.size()
                    << L" dropped=" << history.dropped
                    << L" overflow=" << (history.overflowed ? 1 : 0);
            AppendLog(summary.str());
            for (const auto& record : history.records) {
                AppendLog(OfflineFailureHistoryText(record));
            }
        }
        SetStatus(L"Connected / PLP 1.0 session");
        break;
    case WorkKind::ResetMcu:
        connected_offline_button_ = false;
        oip_download_.SetWindowTextW(L"Download to Board Storage");
        device_info_.SetWindowTextW(L"Bridge MCU reset; scan and reconnect");
        operating_mode_ = {};
        downloaded_package_uuid_.fill(0);
        active_package_uuid_.fill(0);
        UpdateOperatingModeText();
        SetStatus(L"Bridge MCU reset acknowledged / disconnected");
        break;
    case WorkKind::LoadImage:
        package_ = std::move(result->package);
        image_ = std::move(result->image);
        if (package_ && image_) {
            validated_image_source_.Bind(result->image_source_path);
            SetProgressDisplay(WorkKind::LoadImage,
                               10000,
                               result->load_total_bytes,
                               result->load_total_bytes,
                               0,
                               result->program_total_bytes);
            const std::wstring summary = result->message +
                L"\r\nOIP generated automatically in memory; sections: " +
                std::to_wstring(package_->section_count) + L", size: " +
                std::to_wstring(package_->bytes.size()) + L" bytes\r\nPackage UUID: " +
                rl78_programmer::sdk::FormatUuid(package_->package_uuid);
            image_summary_.SetWindowTextW(summary.c_str());
            program_status_.SetWindowTextW(
                L"Image validated. Next step: confirm the target wiring, then click Program Online.");
            SetOfflineWorkflowState(OfflineWorkflowStep::LoadAndValidate,
                                    WorkflowState::Completed);
            if (operating_mode_.active_package_available &&
                operating_mode_.active_package_uuid == package_->package_uuid) {
                downloaded_package_uuid_ = package_->package_uuid;
                SetOfflineWorkflowState(OfflineWorkflowStep::DownloadToSpiFlash,
                                        WorkflowState::Completed);
                SetOfflineWorkflowState(OfflineWorkflowStep::ActivatePackage,
                                        WorkflowState::Completed);
                if (operating_mode_.mode == PLP_OPERATING_MODE_OFFLINE_ARMED) {
                    SetOfflineWorkflowState(OfflineWorkflowStep::ArmOffline,
                                            WorkflowState::Completed);
                    SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                                            WorkflowState::Ready);
                    offline_status_.SetWindowTextW(
                        L"This OIP matches the active package and the bridge is already OFFLINE ARMED. Disconnect the GUI and perform Step 5 on the board.");
                } else {
                    offline_status_.SetWindowTextW(
                        L"Step 1 complete. This generated OIP already matches the bridge's active package. Next step: click Arm Offline.");
                }
            } else {
                offline_status_.SetWindowTextW(
                    L"Step 1 complete: OIP generated and validated in memory. Next step: click Download to SPI Flash.");
            }
        }
        break;
    case WorkKind::ReadSignature:
        program_status_.SetWindowTextW(result->message.c_str());
        break;
    case WorkKind::Download:
        if (package_) downloaded_package_uuid_ = package_->package_uuid;
        active_package_uuid_.fill(0);
        operating_mode_.active_package_available = false;
        operating_mode_.active_package_uuid.fill(0);
        UpdateOperatingModeText();
        SetProgressDisplay(WorkKind::Download, 10000,
                           result->load_total_bytes,
                           result->load_total_bytes, 0, 0);
        SetOfflineWorkflowState(OfflineWorkflowStep::DownloadToSpiFlash,
                                WorkflowState::Completed);
        offline_status_.SetWindowTextW(result->message.c_str());
        break;
    case WorkKind::Activate:
        if (package_) active_package_uuid_ = package_->package_uuid;
        operating_mode_.active_package_available = true;
        operating_mode_.active_package_uuid = active_package_uuid_;
        UpdateOperatingModeText();
        SetOfflineWorkflowState(OfflineWorkflowStep::ActivatePackage,
                                WorkflowState::Completed);
        offline_status_.SetWindowTextW(result->message.c_str());
        break;
    case WorkKind::ArmOffline:
        if (result->operating_mode) {
            operating_mode_ = *result->operating_mode;
            active_package_uuid_ = operating_mode_.active_package_uuid;
        }
        UpdateOperatingModeText();
        SetOfflineWorkflowState(OfflineWorkflowStep::ArmOffline,
                                WorkflowState::Completed);
        SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                                WorkflowState::Ready);
        offline_status_.SetWindowTextW(result->message.c_str());
        break;
    case WorkKind::SwitchOnline:
        if (result->operating_mode) {
            operating_mode_ = *result->operating_mode;
            active_package_uuid_ = operating_mode_.active_package_uuid;
        }
        UpdateOperatingModeText();
        SetOfflineWorkflowState(OfflineWorkflowStep::ArmOffline,
                                WorkflowState::Pending);
        SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                                WorkflowState::Pending);
        offline_status_.SetWindowTextW(result->message.c_str());
        break;
    case WorkKind::LoadBridgeFirmware:
        bridge_firmware_image_ = std::move(result->bridge_firmware_image);
        SetBridgeWorkflowState(BridgeWorkflowStep::LoadAndValidate,
                               WorkflowState::Completed);
        SetProgressDisplay(WorkKind::LoadBridgeFirmware,
                           bridge_firmware_image_ ? 10000 : 0,
                           result->load_total_bytes,
                           result->load_total_bytes,
                           0,
                           result->program_total_bytes);
        if (bridge_fw_device_combo_.GetCurSel() >= 0) {
            bridge_fw_status_.SetWindowTextW(
                (result->message + L"\r\nNext step: click Upload Firmware.").c_str());
        } else if (!bootloader_devices_.empty()) {
            bridge_fw_status_.SetWindowTextW(
                (result->message +
                 L"\r\nNext step: select the intended bootloader, then click Upload Firmware.").c_str());
        } else {
            bridge_fw_status_.SetWindowTextW(
                (result->message +
                 L"\r\nNext step: Return Boot loader, then Scan Bootloader.").c_str());
        }
        break;
    case WorkKind::ReturnBootloader:
        SetBridgeWorkflowState(BridgeWorkflowStep::ReturnBootloader,
                               WorkflowState::Completed);
        connected_offline_button_ = false;
        oip_download_.SetWindowTextW(L"Download to Board Storage");
        device_info_.SetWindowTextW(
            IsUartBoardSelected()
                ? L"Bridge application rebooting to UART ISP bootloader"
                : L"Bridge application rebooting to USB HID bootloader");
        operating_mode_ = {};
        downloaded_package_uuid_.fill(0);
        active_package_uuid_.fill(0);
        UpdateOperatingModeText();
        SetStatus(L"Bootloader entry acknowledged / application disconnected");
        bridge_fw_status_.SetWindowTextW(result->message.c_str());
        break;
    case WorkKind::ScanBridgeBootloader:
        bootloader_devices_ = std::move(result->bootloader_devices);
        bridge_fw_device_combo_.ResetContent();
        for (std::size_t i = 0; i < bootloader_devices_.size(); ++i) {
            const auto& device = bootloader_devices_[i];
            std::wstring label;
            if (device.transport ==
                rl78_programmer::sdk::TransportKind::Uart) {
                label = (device.serial_number.empty()
                             ? device.path
                             : device.serial_number) +
                        L" / UART ISP 115200 8-N-1";
            } else {
                label = L"HID path " + device.path + L" / ";
                label += device.product.empty()
                    ? L"Nuvoton USB HID ISP"
                    : device.product;
                if (!device.serial_number.empty()) {
                    label += L" / " + device.serial_number;
                }
                label += L" / PID 0x";
                std::wostringstream pid;
                pid << std::uppercase << std::hex << std::setw(4)
                    << std::setfill(L'0') << device.product_id;
                label += pid.str();
            }
            bridge_fw_device_combo_.AddString(label.c_str());
        }
        if (bootloader_devices_.size() == 1u) {
            bridge_fw_device_combo_.SetCurSel(0);
        }
        SetBridgeWorkflowState(BridgeWorkflowStep::ScanBootloader,
                               bootloader_devices_.empty() ?
                                   WorkflowState::Failed :
                                   WorkflowState::Completed);
        {
            std::wstring guidance =
                bootloader_devices_.size() > 1u
                    ? (IsUartBoardSelected()
                           ? L"Multiple COM ports found. Select the VCOM wired to PF2/PF3; disconnect unrelated VCOM adapters if the mapping is uncertain."
                           : L"Multiple bootloaders found. Each row includes its HID path. Confirm the intended physical board; if the path-to-board mapping is uncertain, disconnect the other boards and rescan.")
                    : result->message;
            if (!bootloader_devices_.empty() && !bridge_firmware_image_) {
                guidance +=
                    L"\r\nNext step: select the matching Bridge profile and APP BIN, then "
                    L"click Load + Validate. Upload Firmware remains disabled until validation passes.";
            } else if (bridge_fw_device_combo_.GetCurSel() >= 0) {
                guidance += L"\r\nNext step: click Upload Firmware.";
            } else if (!bootloader_devices_.empty()) {
                guidance +=
                    L"\r\nNext step: select the intended bootloader, then click Upload Firmware.";
            }
            bridge_fw_status_.SetWindowTextW(guidance.c_str());
        }
        break;
    case WorkKind::UploadBridgeFirmware:
        SetBridgeWorkflowState(BridgeWorkflowStep::UploadFirmware,
                               WorkflowState::Completed);
        if (bridge_firmware_image_) {
            const std::uint64_t size = bridge_firmware_image_->bytes.size();
            SetProgressDisplay(WorkKind::UploadBridgeFirmware,
                               10000, size, size, size, size);
        }
        if (result->bootloader_info) {
            const auto& info = *result->bootloader_info;
            std::wostringstream text;
            text << result->message << L"\r\nBoot FW 0x" << std::uppercase << std::hex
                 << static_cast<unsigned>(info.firmware_version)
                 << L", PDID 0x" << std::setw(8) << std::setfill(L'0')
                 << info.device_id
                 << L"\r\nApplication 0x" << std::setw(8)
                 << info.application_geometry.application_start
                 << L"..0x" << std::setw(8)
                 << (info.application_geometry.application_end - 1u)
                 << L", CRC @0x" << std::setw(8)
                 << info.application_geometry.crc32_address
                 << L", Board ID 0x" << std::setw(8)
                 << info.application_geometry.board_id;
            bridge_fw_status_.SetWindowTextW(text.str().c_str());
        } else {
            bridge_fw_status_.SetWindowTextW(result->message.c_str());
        }
        break;
    case WorkKind::RunBridgeApplication:
        SetBridgeWorkflowState(BridgeWorkflowStep::RunApplication,
                               WorkflowState::Completed);
        bridge_fw_status_.SetWindowTextW(result->message.c_str());
        break;
    case WorkKind::ProgramOnline:
        active_package_uuid_.fill(0);
        operating_mode_.active_package_available = false;
        operating_mode_.active_package_uuid.fill(0);
        UpdateOperatingModeText();
        [[fallthrough]];
    case WorkKind::ProgramOffline:
        program_status_.SetWindowTextW(result->message.c_str());
        offline_status_.SetWindowTextW(result->message.c_str());
        if (result->job) {
            const bool passed =
                result->job->terminal_result == PLP_TERMINAL_PASS;
            if (result->kind == WorkKind::ProgramOffline) {
                SetOfflineWorkflowState(OfflineWorkflowStep::TriggerProgramming,
                                        passed ? WorkflowState::Completed :
                                                 WorkflowState::Failed);
            }
            const std::uint64_t load_total =
                result->kind == WorkKind::ProgramOnline && package_ ?
                    package_->bytes.size() : 0u;
            const std::uint64_t program_total = result->job->total != 0u ?
                result->job->total : (image_ ? image_->DataSize() : 0u);
            const std::uint64_t programmed = passed && program_total != 0u ?
                program_total : result->job->current;
            const std::uint16_t terminal_basis_points = passed ? 10000u :
                (result->kind == WorkKind::ProgramOnline ?
                    static_cast<std::uint16_t>(2500u +
                        (static_cast<std::uint32_t>(result->job->progress_basis_points) * 3u) / 4u) :
                    result->job->progress_basis_points);
            SetProgressDisplay(result->kind,
                               terminal_basis_points,
                               load_total,
                               load_total,
                               programmed,
                               program_total);
            const std::wstring mode =
                result->kind == WorkKind::ProgramOnline ? L"Online" : L"Offline";
            AddHistory(mode, *result->job);
            AppendLog(JobTerminalText(mode, *result->job));
        }
        break;
    }
    UpdateUiState();
    return 0;
}

bool CMainFrame::IsZeroUuid(const std::array<std::uint8_t, 16>& uuid) {
    return std::all_of(uuid.begin(), uuid.end(), [](std::uint8_t byte) { return byte == 0; });
}

void CMainFrame::OnClose() {
    closing_.store(true);
    cancel_requested_.store(true);
    if (worker_.joinable()) worker_.join();
    if (session_file_logger_.IsOpen()) {
        AppendLog(L"event=APPLICATION_CLOSE");
        session_file_logger_.Close();
    }
    client_.Disconnect();
    bridge_firmware_updater_.Disconnect();
    SaveConfigFromUi();
    CFrameWnd::OnClose();
}
