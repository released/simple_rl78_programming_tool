#include "bridge_firmware_updater.h"

#include <algorithm>
#include <tuple>

#include "hid_transport.h"
#include "nuvo_isp_bootloader_client.h"
#include "rl78_programmer/plp_contract.h"
#include "serial_transport.h"

namespace rl78_programmer::sdk {

class BridgeFirmwareUpdater::Impl {
public:
    detail::NuvoIspBootloaderClient client;
};

BridgeFirmwareUpdater::BridgeFirmwareUpdater()
    : impl_(std::make_unique<Impl>()) {}

BridgeFirmwareUpdater::~BridgeFirmwareUpdater() {
    Disconnect();
}

std::vector<DeviceDescriptor> BridgeFirmwareUpdater::EnumerateBootloaders() {
    auto devices = detail::HidTransport::Enumerate(
        kNuvotonVendorId, kBootloaderProductId, true);
    auto legacy = detail::HidTransport::Enumerate(
        kNuvotonVendorId, kLegacyBootloaderProductId, true);
    devices.insert(devices.end(), legacy.begin(), legacy.end());

    std::sort(devices.begin(), devices.end(), [](const auto& left, const auto& right) {
        return std::tie(left.product_id, left.serial_number, left.path) <
               std::tie(right.product_id, right.serial_number, right.path);
    });
    devices.erase(std::unique(devices.begin(), devices.end(),
                              [](const auto& left, const auto& right) {
                                  return left.path == right.path;
                              }),
                  devices.end());
    return devices;
}

std::vector<DeviceDescriptor> BridgeFirmwareUpdater::EnumerateSerialPorts() {
    return detail::SerialTransport::Enumerate();
}

void BridgeFirmwareUpdater::ConnectUnique(int timeout_ms) {
    const auto devices = EnumerateBootloaders();
    if (devices.empty()) {
        throw ProgrammerException(PLP_ERR_USB_NOT_FOUND,
                                  "no Nuvoton USB HID ISP bootloader was found");
    }
    if (devices.size() != 1u) {
        throw ProgrammerException(
            PLP_ERR_SDK_ARGUMENT,
            "multiple Nuvoton USB HID ISP bootloaders were found; select an explicit descriptor");
    }
    Connect(devices.front(), timeout_ms);
}

void BridgeFirmwareUpdater::Connect(const DeviceDescriptor& descriptor,
                                    int timeout_ms) {
    if (descriptor.path.empty()) {
        throw ProgrammerException(PLP_ERR_SDK_ARGUMENT,
                                  "bootloader transport descriptor path is empty");
    }
    if (descriptor.transport != TransportKind::Uart &&
        ((descriptor.vendor_id != 0u &&
          descriptor.vendor_id != kNuvotonVendorId) ||
         (descriptor.product_id != 0u &&
          descriptor.product_id != kBootloaderProductId &&
          descriptor.product_id != kLegacyBootloaderProductId))) {
        throw ProgrammerException(PLP_ERR_UPDATE_INCOMPATIBLE,
                                  "selected HID descriptor is not a supported Nuvoton bootloader");
    }

    impl_->client.Connect(descriptor, timeout_ms);
    const auto current = impl_->client.CurrentDevice();
    if (current.transport != TransportKind::Uart &&
        (current.vendor_id != kNuvotonVendorId ||
         (current.product_id != kBootloaderProductId &&
          current.product_id != kLegacyBootloaderProductId))) {
        impl_->client.Disconnect();
        throw ProgrammerException(PLP_ERR_UPDATE_INCOMPATIBLE,
                                  "opened HID device is not a supported Nuvoton bootloader");
    }
}

void BridgeFirmwareUpdater::Disconnect() noexcept {
    impl_->client.Disconnect();
}

bool BridgeFirmwareUpdater::IsConnected() const noexcept {
    return impl_->client.IsConnected();
}

DeviceDescriptor BridgeFirmwareUpdater::CurrentDevice() const {
    return impl_->client.CurrentDevice();
}

BridgeBootloaderInfo BridgeFirmwareUpdater::Probe(
    const BridgeFirmwareCancelCallback& cancel,
    int handshake_timeout_ms) {
    return impl_->client.Probe(cancel, handshake_timeout_ms);
}

void BridgeFirmwareUpdater::ProgramApplication(
    const BridgeFirmwareImage& image,
    const BridgeFirmwareProgressCallback& progress,
    const BridgeFirmwareCancelCallback& cancel) {
    const BridgeFirmwareImage validated =
        ValidateImage(image.profile.target, image.bytes);
    impl_->client.ProgramApplication(validated, progress, cancel);
}

void BridgeFirmwareUpdater::RunApplication() {
    impl_->client.RunApplication();
}

void BridgeFirmwareUpdater::ResetBootloader() {
    impl_->client.ResetBootloader();
}

} // namespace rl78_programmer::sdk
