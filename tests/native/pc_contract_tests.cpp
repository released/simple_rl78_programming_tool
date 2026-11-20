#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include "application_worker.h"
#include "image_loading.h"
#include "programming_job.h"
#include "programmer_transport.h"
#include "rl78_programmer/plp_frame.h"

using namespace rl78_programmer;
namespace {
unsigned checks = 0;
void Check(bool value, const char* name) {
    if (!value) throw std::runtime_error(name);
    ++checks;
    std::cout << "PASS: " << name << '\n';
}
template<class Action> void Reject(Action action, const char* name) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, name);
}

app::ProgrammingRequest Request() {
    SRecordImage image;
    image.segments.push_back({0u, {1, 2, 3, 4}});
    OipBuildOptions options;
    options.build_time_ms = 1;
    return {BuildOfflineImagePackage(image, options), options.target_profile,
            {1, 2, 3}, 0};
}

class FakeSdk final : public app::ProgrammingPort {
public:
    unsigned downloads = 0, starts = 0, queries = 0, cancels = 0;
    bool lost_start = false, lost_query = false, wrong_job = false;
    sdk::JobStatus response;
    std::function<void()> during_download;
    std::function<void()> during_query;
    void Download(const OfflineImagePackage&, const sdk::ProgressCallback& progress) override {
        ++downloads;
        if (during_download) during_download();
        if (progress) progress(10000, L"synthetic staged");
    }
    void Start(const std::array<std::uint8_t, 16>& job, std::uint8_t,
               const std::array<std::uint8_t, 16>&,
               const std::array<std::uint8_t, 16>&) override {
        ++starts;
        response.job_uuid = job;
        if (lost_start) throw sdk::ProgrammerException(PLP_ERR_OUTCOME_UNKNOWN, "synthetic lost START reply");
    }
    sdk::JobStatus Query(const std::array<std::uint8_t, 16>&) override {
        ++queries;
        if (during_query) during_query();
        if (lost_query) throw sdk::ProgrammerException(PLP_ERR_USB_TIMEOUT, "synthetic lost query reply");
        auto value = response;
        if (wrong_job) value.job_uuid[0] ^= 1;
        return value;
    }
    void Cancel(const std::array<std::uint8_t, 16>&) override { ++cancels; }
    FakeSdk() {
        response.operation_state = PLP_OPERATION_COMPLETED;
        response.terminal_result = PLP_TERMINAL_PASS;
        response.progress_basis_points = 10000;
    }
};

class FakeTransport final : public sdk::detail::ProgrammerTransport {
public:
    bool open = true, lose_start = false, wrong_query = false;
    unsigned starts = 0;
    std::uint32_t rejection = 0;
    std::array<std::uint8_t, PLP_REPORT_SIZE> request{};
    void Close() noexcept override { open = false; }
    bool IsOpen() const noexcept override { return open; }
    sdk::DeviceDescriptor CurrentDevice() const override { return {}; }
    void Write(const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) override { request = report; }
    std::array<std::uint8_t, PLP_REPORT_SIZE> Read(int) override {
        RppPlpFrame frame{};
        if (Rpp_PlpDecode(request.data(), &frame) != PLP_OK) throw std::runtime_error("bad synthetic request");
        std::vector<std::uint8_t> body;
        std::uint32_t status = PLP_OK;
        if (frame.command == PLP_CMD_NEGOTIATE) {
            body.resize(32);
            body[0] = PLP_VERSION_MAJOR;
            // No replay cache: a mutating START still must be sent once.
        } else if (frame.command == PLP_CMD_OPEN_SESSION) {
            body.resize(24, 1);
        } else if (frame.command == PLP_CMD_PROGRAM_START) {
            ++starts;
            if (lose_start) throw sdk::ProgrammerException(PLP_ERR_USB_TIMEOUT, "synthetic accepted, reply lost");
            status = rejection;
            body.assign(frame.payload, frame.payload + 16);
        } else if (frame.command == PLP_CMD_PROGRAM_GET_JOB) {
            body.resize(42);
            std::copy_n(frame.payload, 16, body.begin());
            if (wrong_query) body[0] ^= 1;
            body[16] = PLP_OPERATION_COMPLETED;
            body[18] = PLP_TERMINAL_PASS;
        }
        std::vector<std::uint8_t> payload(PLP_COMMON_RESPONSE_SIZE + body.size(), 0);
        Rpp_WriteLe32(payload.data(), status);
        std::copy(body.begin(), body.end(), payload.begin() + PLP_COMMON_RESPONSE_SIZE);
        std::array<std::uint8_t, PLP_REPORT_SIZE> report{};
        if (Rpp_PlpEncode(PLP_VERSION_MAJOR, PLP_VERSION_MINOR,
            static_cast<std::uint8_t>(PLP_FLAG_RESPONSE | (status ? PLP_FLAG_NAK : 0)),
            frame.command, frame.sequence, payload.data(), static_cast<std::uint16_t>(payload.size()),
            report.data()) != PLP_OK) throw std::runtime_error("bad synthetic response");
        return report;
    }
    void WriteRaw(const std::array<std::uint8_t, PLP_REPORT_SIZE>& report) override { Write(report); }
    std::array<std::uint8_t, PLP_REPORT_SIZE> ReadRaw(int timeout) override { return Read(timeout); }
};

void TestProgramming() {
    auto request = Request();
    auto run = [&](FakeSdk& sdk, const app::StopRequested& cancel = {}) {
        return app::RunProgramming(sdk, request, cancel, {}, {},
            std::chrono::milliseconds(1), std::chrono::milliseconds(0));
    };
    FakeSdk sdk;
    Check(run(sdk).terminal_result == PLP_TERMINAL_PASS && sdk.starts == 1, "approved snapshot reaches authoritative PASS");
    FakeSdk failed;
    failed.response.operation_state = PLP_OPERATION_FAILED;
    failed.response.terminal_result = PLP_TERMINAL_FAIL;
    failed.response.error = PLP_ERR_DRIVER_PROFILE;
    Check(run(failed).terminal_result == PLP_TERMINAL_FAIL, "authoritative failure stays distinct from unknown");
    FakeSdk cancelled;
    Check(run(cancelled, [] { return true; }).terminal_result == PLP_TERMINAL_CANCELLED &&
          cancelled.starts == 0 && cancelled.downloads == 0, "cancel before staging has no SDK side effect");
    bool cancel = false;
    FakeSdk staging;
    staging.during_download = [&] { cancel = true; };
    Check(run(staging, [&] { return cancel; }).terminal_result == PLP_TERMINAL_CANCELLED &&
          staging.starts == 0, "cancel during staging never starts target programming");
    FakeSdk lost;
    lost.lost_start = true;
    Check(run(lost).terminal_result == PLP_TERMINAL_PASS && lost.starts == 1 && lost.queries == 1,
          "lost START reply reconciles original job without replay");
    FakeSdk missing;
    missing.lost_query = true;
    const auto unknown = run(missing);
    Check(unknown.terminal_result == PLP_TERMINAL_OUTCOME_UNKNOWN && unknown.job_uuid == request.job_uuid &&
          unknown.error == PLP_ERR_USB_TIMEOUT, "lost query retains original job and causal error as unknown");
    FakeSdk wrong;
    wrong.wrong_job = true;
    Check(run(wrong).terminal_result == PLP_TERMINAL_OUTCOME_UNKNOWN, "foreign job PASS cannot complete current operation");
    FakeSdk pending;
    pending.response.operation_state = PLP_OPERATION_CANCEL_PENDING;
    pending.response.terminal_result = PLP_TERMINAL_NONE;
    pending.response.progress_basis_points = 0;
    pending.response.stage = 7;
    bool stop = false;
    pending.during_download = [&] { stop = false; };
    const auto result = app::RunProgramming(pending, request, [&] { return stop; }, {},
        [&](const app::ProgrammingProgress& value) { if (value.job.job_uuid == request.job_uuid) stop = true; },
        std::chrono::milliseconds(2), std::chrono::milliseconds(0));
    Check(result.terminal_result == PLP_TERMINAL_OUTCOME_UNKNOWN && pending.cancels == 1 && result.stage == 7,
          "cancel ACK pending never becomes local CANCELLED and is sent once");
    FakeSdk completed_after_cancel;
    stop = false;
    completed_after_cancel.response.operation_state = PLP_OPERATION_RUNNING;
    completed_after_cancel.response.terminal_result = PLP_TERMINAL_NONE;
    completed_after_cancel.during_query = [&] {
        if (completed_after_cancel.queries == 2) {
            completed_after_cancel.response.operation_state = PLP_OPERATION_COMPLETED;
            completed_after_cancel.response.terminal_result = PLP_TERMINAL_PASS;
        }
    };
    const auto raced = app::RunProgramming(completed_after_cancel, request, [&] { return stop; }, {},
        [&](const app::ProgrammingProgress& value) { if (value.job.job_uuid == request.job_uuid) stop = true; },
        std::chrono::milliseconds(100), std::chrono::milliseconds(0));
    Check(raced.terminal_result == PLP_TERMINAL_PASS && completed_after_cancel.cancels == 1,
          "authoritative PASS wins a concurrent local cancel request");
    FakeSdk invalid;
    invalid.response.error = PLP_ERR_PROGRAM_JOB_CONFLICT;
    Check(run(invalid).terminal_result == PLP_TERMINAL_OUTCOME_UNKNOWN, "PASS with error is rejected");
    request.profile = Rl78G15_4KTargetProfile();
    FakeSdk mismatch;
    Reject([&] { run(mismatch); }, "mismatched profile rejected before staging");
    Check(mismatch.downloads == 0, "profile rejection has no device side effect");
    request = Request();
    request.package.target_profile_uuid[0] ^= 1;
    Reject([&] { run(mismatch); }, "package metadata and serialized profile must agree");
    request = Request();
    ++request.profile.version[2];
    Reject([&] { run(mismatch); }, "package and selected profile version must agree");
    auto protected_request = Request();
    protected_request.profile.regions[2].end_address = 1;
    Reject([&] { app::ValidateProgrammingRequest(protected_request); }, "out-of-profile range fails preflight");
    Check(ClassifyTargetAddress(Rl78G15_8KTargetProfile(), 0xC0).sensitive &&
          ClassifyTargetAddress(Rl78G15_8KTargetProfile(), 0xC4).sensitive &&
          !ClassifyTargetAddress(Rl78G15_8KTargetProfile(), 0xCE).sensitive,
          "option/security boundaries retain protected classification");
    SRecordImage image;
    image.segments.push_back({0xBF, {1, 2, 3, 4, 5, 6}});
    auto protected_profile = Rl78G15_8KTargetProfile();
    protected_profile.regions[0].sensitive = false;
    OipBuildOptions options;
    options.target_profile = protected_profile;
    protected_request = {BuildOfflineImagePackage(image, options), Rl78G15_8KTargetProfile(), {1}, 0};
    Reject([&] { app::ValidateProgrammingRequest(protected_request); }, "serialized code cannot downgrade protected-region policy");
}

void TestTransport() {
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    sdk::ProgrammerClient client(std::move(transport), 100);
    const auto request = Request();
    fake->lose_start = true;
    std::uint32_t error = 0;
    try { client.StartProgramWithId(request.job_uuid, 0, request.package.package_uuid, request.profile.uuid); }
    catch (const sdk::ProgrammerException& ex) { error = ex.status(); }
    Check(error == PLP_ERR_OUTCOME_UNKNOWN && fake->starts == 1,
          "real SDK over fake transport sends uncertain START once without replay cache");
    fake->lose_start = false;
    fake->rejection = PLP_ERR_DRIVER_PROFILE;
    error = 0;
    try { client.StartProgramWithId(request.job_uuid, 0, request.package.package_uuid, request.profile.uuid); }
    catch (const sdk::ProgrammerException& ex) { error = ex.status(); }
    Check(error == PLP_ERR_DRIVER_PROFILE, "authoritative START NAK keeps rejection distinct from unknown");
    fake->wrong_query = true;
    Reject([&] { client.QueryJob(request.job_uuid); }, "real SDK rejects mismatched job response");
}

void TestWorker() {
    app::ApplicationWorker<int, int> worker;
    std::mutex mutex;
    std::condition_variable condition;
    bool released = false;
    Check(worker.Start([&] {
        std::unique_lock<std::mutex> guard(mutex);
        condition.wait(guard, [&] { return released; });
        worker.Publish(1);
        worker.Publish(2);
        return std::make_unique<int>(7);
    }), "single-flight worker accepts first task");
    Check(!worker.Start([] { return std::make_unique<int>(9); }), "double trigger rejected while running");
    {
        std::lock_guard<std::mutex> guard(mutex);
        released = true;
    }
    condition.notify_one();
    const auto expires = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::unique_ptr<int> value;
    bool remained_busy = true;
    while (!value && std::chrono::steady_clock::now() < expires) {
        remained_busy = remained_busy && worker.IsBusy();
        value = worker.TakeResult();
        std::this_thread::yield();
    }
    Check(remained_busy, "unconsumed completion remains busy");
    Check(value && *value == 7 && !worker.IsBusy(), "completion consumed exactly once without HWND notification");
    Check(!worker.TakeResult(), "completion cannot be consumed twice");
    Check(worker.Start([] { return std::make_unique<int>(8); }), "new generation admitted after consumption");
    while (!(value = worker.TakeResult()) && std::chrono::steady_clock::now() < expires) std::this_thread::yield();
    Check(value && *value == 8, "new generation cannot receive prior result");
}

void TestImages(const std::filesystem::path& work) {
    const auto path = work / "synthetic.mot";
    {
        std::ofstream out(path, std::ios::binary);
        out << ":0400000001020304F2\n:00000001FF\n";
    }
    const auto loaded = app::LoadTargetImage(path, Rl78G15_8KTargetProfile());
    Check(loaded.intel_hex_mot && loaded.image.DataSize() == 4 &&
          loaded.package.target_profile_uuid == Rl78G15_8KTargetProfile().uuid,
          "image service content-detects MOT and binds exact target profile");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "S107000001020304EE\nS9030000FC\n";
    }
    Check(app::LoadTargetImage(path, Rl78G15_8KTargetProfile()).image.DataSize() == 4,
          "image service preserves S-record ingestion");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << ":0400000001020304F3\n:00000001FF\n";
    }
    Reject([&] { app::LoadTargetImage(path, Rl78G15_8KTargetProfile()); },
           "invalid addressed text never falls back to binary");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.seekp(4 * 1024 * 1024);
        out.put('x');
    }
    Reject([&] { app::LoadTargetImage(path, Rl78G15_8KTargetProfile()); },
           "oversized source rejected before allocation/parsing");
    const auto binary = work / "synthetic.bin";
    {
        std::ofstream out(binary, std::ios::binary);
        const char bytes[] = {1, 2, 3, 4};
        out.write(bytes, sizeof(bytes));
    }
    Check(app::LoadTargetImage(binary, Rl78G15_8KTargetProfile()).image.DataSize() == 4,
          "explicit raw BIN remains supported");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) throw std::runtime_error("isolated fixture directory required");
        TestProgramming();
        TestTransport();
        TestWorker();
        TestImages(argv[1]);
        std::cout << "PC contract checks passed: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
