#pragma once

#include <afxcmn.h>
#include <afxdlgs.h>
#include <afxwin.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app_config.h"
#include "bridge_firmware_updater.h"
#include "programmer_sdk.h"
#include "session_file_logger.h"
#include "validated_image_source.h"

class CVerticallyCenteredEdit : public CEdit {
public:
    void UpdateTextRectangle();

protected:
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnChar(UINT character, UINT repeat_count, UINT flags);
    afx_msg LRESULT OnSetFont(WPARAM font, LPARAM redraw);
    DECLARE_MESSAGE_MAP()
};

class CVerticallyCenteredComboBox : public CComboBox {
public:
    void DrawItem(LPDRAWITEMSTRUCT draw_item) override;
    void MeasureItem(LPMEASUREITEMSTRUCT measure_item) override;
};

class CMainFrame : public CFrameWnd {
public:
    CMainFrame();
    ~CMainFrame() override;

protected:
    BOOL PreCreateWindow(CREATESTRUCT& cs) override;
    afx_msg int OnCreate(LPCREATESTRUCT create_struct);
    afx_msg void OnSize(UINT type, int cx, int cy);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* min_max);
    afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* window, UINT control_color);
    afx_msg void OnTabChanged(NMHDR* header, LRESULT* result);
    afx_msg void OnScan();
    afx_msg void OnConnect();
    afx_msg void OnDisconnect();
    afx_msg void OnResetMcu();
    afx_msg void OnBoardProfileChanged();
    afx_msg void OnBrowseMot();
    afx_msg void OnMotPathChanged();
    afx_msg void OnLoadMot();
    afx_msg void OnTargetProfileChanged();
    afx_msg void OnReadSignature();
    afx_msg void OnProgramOnline();
    afx_msg void OnCancelJob();
    afx_msg void OnSaveOip();
    afx_msg void OnDownloadOip();
    afx_msg void OnActivateOip();
    afx_msg void OnProgramOffline();
    afx_msg void OnArmOffline();
    afx_msg void OnSwitchOnline();
    afx_msg void OnBrowseBridgeFirmware();
    afx_msg void OnBridgeFirmwareProfileChanged();
    afx_msg void OnLoadBridgeFirmware();
    afx_msg void OnReturnBootloader();
    afx_msg void OnScanBridgeBootloader();
    afx_msg void OnUploadBridgeFirmware();
    afx_msg void OnRunBridgeApplication();
    afx_msg void OnCancelBridgeFirmware();
    afx_msg void OnClearHistory();
    afx_msg void OnClearLog();
    afx_msg void OnToggleFileLog();
    afx_msg LRESULT OnWorkerProgress(WPARAM, LPARAM parameter);
    afx_msg LRESULT OnWorkerDone(WPARAM, LPARAM parameter);
    afx_msg void OnClose();
    DECLARE_MESSAGE_MAP()

private:
    enum class WorkKind {
        Scan,
        Connect,
        ResetMcu,
        LoadImage,
        ReadSignature,
        Download,
        Activate,
        ArmOffline,
        SwitchOnline,
        LoadBridgeFirmware,
        ReturnBootloader,
        ScanBridgeBootloader,
        UploadBridgeFirmware,
        RunBridgeApplication,
        ProgramOnline,
        ProgramOffline
    };

    enum class ProgrammerBoardProfile : int {
        Nulink2MeUsb = 0,
        NumakerM032Usb = 1,
        M032TestBoardUart = 2
    };

    enum class BridgeWorkflowStep : std::size_t {
        LoadAndValidate,
        ReturnBootloader,
        ScanBootloader,
        UploadFirmware,
        RunApplication,
        Count
    };

    enum class OfflineWorkflowStep : std::size_t {
        LoadAndValidate,
        DownloadToSpiFlash,
        ActivatePackage,
        ArmOffline,
        TriggerProgramming,
        Count
    };

    enum class WorkflowState {
        Pending,
        InProgress,
        Ready,
        Completed,
        Failed
    };

    struct WorkerProgress {
        WorkKind kind = WorkKind::ProgramOnline;
        std::uint16_t basis_points = 0;
        std::uint64_t loaded_bytes = 0;
        std::uint64_t load_total_bytes = 0;
        std::uint64_t programmed_bytes = 0;
        std::uint64_t program_total_bytes = 0;
        std::wstring message;
    };

    struct WorkerResult {
        WorkKind kind = WorkKind::Scan;
        bool success = false;
        std::wstring message;
        std::vector<rl78_programmer::sdk::DeviceDescriptor> devices;
        std::vector<rl78_programmer::sdk::DeviceDescriptor> bootloader_devices;
        std::optional<rl78_programmer::OfflineImagePackage> package;
        std::optional<rl78_programmer::SRecordImage> image;
        std::optional<rl78_programmer::sdk::BridgeFirmwareImage> bridge_firmware_image;
        std::optional<rl78_programmer::sdk::BridgeBootloaderInfo> bootloader_info;
        std::optional<rl78_programmer::sdk::ProgrammerInfo> programmer_info;
        std::optional<rl78_programmer::sdk::TargetSignature> signature;
        std::optional<rl78_programmer::sdk::JobStatus> job;
        std::optional<rl78_programmer::sdk::OperatingModeInfo> operating_mode;
        std::optional<rl78_programmer::sdk::OfflineResult> offline_result;
        std::optional<rl78_programmer::sdk::OfflineFailureHistory>
            offline_failure_history;
        std::uint32_t error_status = 0;
        std::uint32_t capabilities = 0;
        std::uint64_t load_total_bytes = 0;
        std::uint64_t program_total_bytes = 0;
        std::wstring image_source_path;
    };

    void CreateControls();
    void ApplyFont();
    void LayoutControls(int cx, int cy);
    void ShowSelectedTab();
    void UpdateUiState();
    void SetStatus(const std::wstring& text);
    void AppendLogToView(const std::wstring& line);
    void AppendLog(const std::wstring& text);
    void DisableFileLogAfterError(const std::wstring& error);
    void PersistFileLogPreference();
    void AddHistory(const std::wstring& mode,
                    const rl78_programmer::sdk::JobStatus& job);
    void UpdateOperatingModeText();
    void LoadConfigToUi();
    void SaveConfigFromUi();
    std::uint16_t ReadHexControl(const CEdit& edit, std::uint16_t fallback) const;
    std::wstring ControlText(const CWnd& control) const;
    void StartWorker(WorkKind kind,
                     std::function<std::unique_ptr<WorkerResult>()> work);
    void JoinCompletedWorker();
    void PostProgress(WorkKind kind,
                      std::uint16_t basis_points,
                      const std::wstring& message,
                      std::uint64_t loaded_bytes = 0,
                      std::uint64_t load_total_bytes = 0,
                      std::uint64_t programmed_bytes = 0,
                      std::uint64_t program_total_bytes = 0);
    void SetProgressDisplay(WorkKind kind,
                            std::uint16_t basis_points,
                            std::uint64_t loaded_bytes = 0,
                            std::uint64_t load_total_bytes = 0,
                            std::uint64_t programmed_bytes = 0,
                            std::uint64_t program_total_bytes = 0);
    void SetBridgeWorkflowState(BridgeWorkflowStep step,
                                WorkflowState state);
    void SetBridgeWorkflowFailure(WorkKind kind);
    void SetOfflineWorkflowState(OfflineWorkflowStep step,
                                 WorkflowState state);
    void SetOfflineWorkflowFailure(WorkKind kind);
    void ResetOfflineWorkflowAfter(OfflineWorkflowStep step);
    std::unique_ptr<WorkerResult> RunProgram(WorkKind kind, std::uint8_t source_kind);
    rl78_programmer::TargetProfile SelectedTargetProfile() const;
    rl78_programmer::sdk::BridgeFirmwareTarget SelectedBridgeFirmwareTarget() const;
    ProgrammerBoardProfile SelectedBoardProfile() const;
    bool IsUartBoardSelected() const;
    void ApplyBoardProfile();
    static bool IsZeroUuid(const std::array<std::uint8_t, 16>& uuid);

    CFont ui_font_;
    CStatic vid_label_;
    CVerticallyCenteredEdit vid_edit_;
    CStatic pid_label_;
    CVerticallyCenteredEdit pid_edit_;
    CButton scan_button_;
    CVerticallyCenteredComboBox device_combo_;
    CButton connect_button_;
    CButton disconnect_button_;
    CButton reset_mcu_button_;
    CStatic status_text_;
    CStatic device_info_;
    CStatic board_profile_label_;
    CComboBox board_profile_combo_;
    CTabCtrl tab_;

    CStatic mot_label_;
    CStatic target_profile_label_;
    CComboBox target_profile_combo_;
    CVerticallyCenteredEdit mot_path_;
    CButton mot_browse_;
    CButton mot_load_;
    CStatic image_summary_;
    CButton signature_button_;
    CButton online_button_;
    CButton cancel_button_;
    CProgressCtrl progress_;
    CStatic program_progress_text_;
    CStatic program_status_;

    CButton oip_save_;
    CButton oip_download_;
    CButton oip_activate_;
    CButton offline_button_;
    CStatic operating_mode_text_;
    CButton arm_offline_button_;
    CButton switch_online_button_;
    CProgressCtrl offline_progress_;
    CStatic offline_progress_text_;
    std::array<CStatic,
               static_cast<std::size_t>(OfflineWorkflowStep::Count)> offline_steps_;
    CStatic offline_status_;

    CStatic bridge_fw_profile_label_;
    CComboBox bridge_fw_profile_combo_;
    CStatic bridge_fw_path_label_;
    CVerticallyCenteredEdit bridge_fw_path_;
    CButton bridge_fw_browse_;
    CButton bridge_fw_load_;
    CButton bridge_fw_return_boot_;
    CButton bridge_fw_scan_boot_;
    CComboBox bridge_fw_device_combo_;
    CButton bridge_fw_upload_;
    CButton bridge_fw_run_;
    CButton bridge_fw_cancel_;
    CProgressCtrl bridge_fw_progress_;
    CStatic bridge_fw_progress_text_;
    std::array<CStatic,
               static_cast<std::size_t>(BridgeWorkflowStep::Count)> bridge_fw_steps_;
    CStatic bridge_fw_status_;

    CListCtrl history_;
    CButton history_clear_;
    CEdit log_edit_;
    CButton log_clear_;
    CButton log_file_enable_;
    CStatic log_file_status_;

    AppConfig config_;
    rl78_programmer::sdk::ProgrammerClient client_;
    rl78_programmer::sdk::BridgeFirmwareUpdater bridge_firmware_updater_;
    std::vector<rl78_programmer::sdk::DeviceDescriptor> devices_;
    std::optional<rl78_programmer::OfflineImagePackage> package_;
    std::optional<rl78_programmer::SRecordImage> image_;
    rl78_programmer::app::ValidatedImageSource validated_image_source_;
    std::optional<rl78_programmer::sdk::BridgeFirmwareImage> bridge_firmware_image_;
    std::vector<rl78_programmer::sdk::DeviceDescriptor> bootloader_devices_;
    std::array<WorkflowState,
               static_cast<std::size_t>(BridgeWorkflowStep::Count)> bridge_fw_step_states_{};
    std::array<WorkflowState,
               static_cast<std::size_t>(OfflineWorkflowStep::Count)> offline_step_states_{};
    std::array<std::uint8_t, 16> downloaded_package_uuid_{};
    std::array<std::uint8_t, 16> active_package_uuid_{};
    rl78_programmer::sdk::OperatingModeInfo operating_mode_{};
    std::array<std::uint8_t, 16> active_job_uuid_{};
    rl78_programmer::app::SessionFileLogger session_file_logger_;
    bool connected_offline_button_ = false;
    std::thread worker_;
    std::atomic<bool> worker_running_{false};
    std::atomic<bool> cancel_requested_{false};
    std::atomic<bool> closing_{false};
};
