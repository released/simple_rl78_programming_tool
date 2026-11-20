#pragma once

#include <chrono>
#include <functional>
#include "programmer_sdk.h"

namespace rl78_programmer::app {

class ProgrammingPort {
public:
    virtual ~ProgrammingPort() = default;
    virtual void Download(const OfflineImagePackage&, const sdk::ProgressCallback&) = 0;
    virtual void Start(const std::array<std::uint8_t, 16>& job, std::uint8_t source,
                       const std::array<std::uint8_t, 16>& package,
                       const std::array<std::uint8_t, 16>& profile) = 0;
    virtual sdk::JobStatus Query(const std::array<std::uint8_t, 16>& job) = 0;
    virtual void Cancel(const std::array<std::uint8_t, 16>& job) = 0;
};

class SdkProgrammingPort final : public ProgrammingPort {
public:
    explicit SdkProgrammingPort(sdk::ProgrammerClient& client) : client_(client) {}
    void Download(const OfflineImagePackage& package, const sdk::ProgressCallback& progress) override;
    void Start(const std::array<std::uint8_t, 16>& job, std::uint8_t source,
               const std::array<std::uint8_t, 16>& package,
               const std::array<std::uint8_t, 16>& profile) override;
    sdk::JobStatus Query(const std::array<std::uint8_t, 16>& job) override;
    void Cancel(const std::array<std::uint8_t, 16>& job) override;
private:
    sdk::ProgrammerClient& client_;
};

struct ProgrammingRequest {
    OfflineImagePackage package;
    TargetProfile profile;
    std::array<std::uint8_t, 16> job_uuid{};
    std::uint8_t source_kind = 0;
};

struct ProgrammingProgress {
    sdk::JobStatus job;
    std::uint16_t basis_points = 0;
    std::uint64_t loaded = 0;
    std::uint64_t load_total = 0;
};

using StopRequested = std::function<bool()>;
using ProgrammingProgressCallback = std::function<void(const ProgrammingProgress&)>;

void ValidateProgrammingRequest(const ProgrammingRequest& request);
sdk::JobStatus RunProgramming(ProgrammingPort& port, const ProgrammingRequest& request,
    const StopRequested& cancel, const StopRequested& closing,
    const ProgrammingProgressCallback& progress,
    std::chrono::milliseconds deadline = std::chrono::minutes(10),
    std::chrono::milliseconds poll_interval = std::chrono::milliseconds(250));

} // namespace rl78_programmer::app
