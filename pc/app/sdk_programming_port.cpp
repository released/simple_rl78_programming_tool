#include "programming_job.h"

namespace rl78_programmer::app {

void SdkProgrammingPort::Download(const OfflineImagePackage& package,
                                  const sdk::ProgressCallback& progress) {
    client_.DownloadPackage(package, progress);
}
void SdkProgrammingPort::Start(const std::array<std::uint8_t, 16>& job, std::uint8_t source,
    const std::array<std::uint8_t, 16>& package, const std::array<std::uint8_t, 16>& profile) {
    client_.StartProgramWithId(job, source, package, profile);
}
sdk::JobStatus SdkProgrammingPort::Query(const std::array<std::uint8_t, 16>& job) {
    return client_.QueryJob(job);
}
void SdkProgrammingPort::Cancel(const std::array<std::uint8_t, 16>& job) {
    static_cast<void>(client_.CancelJob(job));
}

} // namespace rl78_programmer::app
