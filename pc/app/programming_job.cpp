#include "programming_job.h"

#include <algorithm>
#include <thread>
#include "rl78_programmer/integrity.h"
#include "rl78_programmer/oip_format.h"
#include "rl78_programmer/plp_frame.h"

namespace rl78_programmer::app {

void ValidateProgrammingRequest(const ProgrammingRequest& request) {
    ValidateTargetProfile(request.profile);
    RppOipInfo info{};
    const auto& package = request.package;
    if (package.bytes.size() > 1024u * 1024u) {
        throw sdk::ProgrammerException(PLP_ERR_SDK_ARGUMENT, "programming package exceeds host budget");
    }
    const auto status = Rpp_OipValidateBasic(package.bytes.data(), package.bytes.size(), &info);
    if (status != PLP_OK) throw sdk::ProgrammerException(status, "invalid programming package");
    const bool zero_job = std::all_of(request.job_uuid.begin(), request.job_uuid.end(),
                                    [](std::uint8_t value) { return value == 0; });
    std::array<std::uint8_t, 32> digest{};
    Rpp_Sha256(package.bytes.data(), package.bytes.size(), digest.data());
    if (zero_job || request.source_kind > 1 || digest != package.sha256 ||
        package.crc32 != Rpp_ReadLe32(package.bytes.data() + RPP_OIP_PACKAGE_CRC_OFFSET) ||
        request.profile.uuid != package.target_profile_uuid ||
        !std::equal(request.profile.version.begin(), request.profile.version.end(), package.bytes.data() + 104) ||
        !std::equal(request.profile.uuid.begin(), request.profile.uuid.end(), info.target_profile_uuid) ||
        !std::equal(package.package_uuid.begin(), package.package_uuid.end(), info.package_uuid)) {
        throw sdk::ProgrammerException(PLP_ERR_DRIVER_PROFILE, "programming snapshot identity mismatch");
    }
    const auto required_flag = request.source_kind == 0 ?
        RPP_OIP_FLAG_ALLOW_ONLINE : RPP_OIP_FLAG_ALLOW_OFFLINE;
    if ((info.flags & required_flag) == 0) {
        throw sdk::ProgrammerException(PLP_ERR_DRIVER_PROFILE, "package disallows selected programming mode");
    }
    // Preserve every profile classification, including narrower option/security
    // overlays. A code section cannot silently swallow a protected range.
    for (std::uint16_t i = 0; i < info.section_count; ++i) {
        const auto* section = package.bytes.data() + info.descriptor_offset +
                              static_cast<std::size_t>(i) * RPP_OIP_DESCRIPTOR_SIZE;
        const auto address = Rpp_ReadLe32(section + 8);
        const auto length = Rpp_ReadLe32(section + 12);
        if (length > 1024u * 1024u || address > UINT32_MAX - length) {
            throw sdk::ProgrammerException(PLP_ERR_DRIVER_PROFILE, "section address/size exceeds host budget");
        }
        for (std::uint32_t offset = 0; offset < length; ++offset) {
            const auto& region = ClassifyTargetAddress(request.profile, address + offset);
            if (region.section_type != Rpp_ReadLe16(section) || region.policy_class != section[81] ||
                region.sensitive != ((Rpp_ReadLe16(section + 2) & RPP_OIP_SECTION_SENSITIVE) != 0)) {
                throw sdk::ProgrammerException(PLP_ERR_DRIVER_PROFILE, "package changes protected-region policy");
            }
        }
    }
}

sdk::JobStatus RunProgramming(ProgrammingPort& port, const ProgrammingRequest& request,
    const StopRequested& cancel, const StopRequested& closing,
    const ProgrammingProgressCallback& progress, std::chrono::milliseconds deadline,
    std::chrono::milliseconds poll_interval) {
    ValidateProgrammingRequest(request);
    sdk::JobStatus last;
    last.job_uuid = request.job_uuid;
    const auto cancelled = [&]() { return (cancel && cancel()) || (closing && closing()); };
    const auto before_start_cancel = [&]() {
        last.operation_state = PLP_OPERATION_CANCELLED;
        last.terminal_result = PLP_TERMINAL_CANCELLED;
        return last;
    };
    if (cancelled()) return before_start_cancel();
    ProgrammingProgress update;
    if (request.source_kind == 0) {
        update.load_total = request.package.bytes.size();
        port.Download(request.package, [&](std::uint16_t value, const std::wstring&) {
            update.loaded = update.load_total * value / 10000u;
            update.basis_points = static_cast<std::uint16_t>(value / 4u);
            if (progress) progress(update);
        });
    }
    // Staging cancellation never issues PROGRAM_START. Bridge transfer completion
    // is not a target-programming success or an implicit erase authorization.
    if (cancelled()) return before_start_cancel();
    bool started = false;
    const auto unknown = [&](std::uint32_t error) {
        last.operation_state = PLP_OPERATION_FAILED;
        last.terminal_result = PLP_TERMINAL_OUTCOME_UNKNOWN;
        last.error = error;
        return last;
    };
    try {
        try {
            port.Start(request.job_uuid, request.source_kind, request.package.package_uuid,
                       request.profile.uuid);
        } catch (const sdk::ProgrammerException& error) {
            if (error.status() != PLP_ERR_OUTCOME_UNKNOWN) throw;
            // One read-only reconciliation using the same preallocated job UUID.
            // Never create another programming request after a lost START reply.
        }
        started = true;
        const auto expires = std::chrono::steady_clock::now() + deadline;
        auto close_expires = std::chrono::steady_clock::time_point::max();
        bool cancel_sent = false;
        for (;;) {
            if (cancelled() && !cancel_sent) {
                port.Cancel(request.job_uuid);
                cancel_sent = true;
            }
            const auto status = port.Query(request.job_uuid);
            if (status.job_uuid != request.job_uuid || status.operation_state > PLP_OPERATION_CANCELLED ||
                status.terminal_result > PLP_TERMINAL_OUTCOME_UNKNOWN ||
                status.progress_basis_points > 10000u ||
                (status.terminal_result == PLP_TERMINAL_PASS &&
                 (status.operation_state != PLP_OPERATION_COMPLETED || status.error != PLP_OK))) {
                return unknown(PLP_ERR_PROGRAM_JOB_CONFLICT);
            }
            last = status;
            update.job = last;
            update.basis_points = request.source_kind == 0 ?
                static_cast<std::uint16_t>(2500u + status.progress_basis_points * 3u / 4u) :
                status.progress_basis_points;
            if (progress) progress(update);
            if (status.operation_state >= PLP_OPERATION_COMPLETED) {
                if (status.terminal_result == PLP_TERMINAL_NONE) return unknown(PLP_ERR_OUTCOME_UNKNOWN);
                return status;
            }
            const auto now = std::chrono::steady_clock::now();
            if (closing && closing() && close_expires == std::chrono::steady_clock::time_point::max()) {
                close_expires = now + std::chrono::seconds(2);
            }
            if (now >= expires || now >= close_expires) return unknown(PLP_ERR_OUTCOME_UNKNOWN);
            std::this_thread::sleep_for(poll_interval);
        }
    } catch (const sdk::ProgrammerException& error) {
        if (!started) throw;
        return unknown(error.status());
    } catch (...) {
        if (!started) throw;
        return unknown(PLP_ERR_SYSTEM_INTERNAL);
    }
}

} // namespace rl78_programmer::app
