#include "QcaBtUSBDevice.hpp"
#include "QcaBtPayload.hpp"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>

#define super IOService
OSDefineMetaClassAndStructors(QcaBtUSBDevice, IOService)

IOReturn QcaBtUSBDevice::sendVendorRead(IOUSBHostDevice *dev, UInt8 request, void *buffer, UInt16 length) {
StandardUSB::DeviceRequest req{};
req.bmRequestType = StandardUSB::kDeviceRequestDirectionIn |
StandardUSB::kDeviceRequestTypeVendor |
StandardUSB::kDeviceRequestRecipientDevice;
req.bRequest      = request;
req.wValue        = 0;
req.wIndex        = 0;
req.wLength       = length;

uint32_t transferred = 0;
IOReturn ret = dev->deviceRequest(this, req, buffer, transferred, kControlTimeoutMs);
if (ret != kIOReturnSuccess) {
IOLog("QcaBtMac: vendor read 0x%02x failed (0x%08x)\n", request, ret);
return ret;
}
if (transferred != length) {
IOLog("QcaBtMac: vendor read 0x%02x short transfer (%u/%u)\n", request, transferred, length);
return kIOReturnUnderrun;
}
return kIOReturnSuccess;
}

IOReturn QcaBtUSBDevice::downloadBlob(IOUSBHostDevice *dev, IOUSBHostPipe *bulkOutPipe,
const UInt8 *data, UInt32 size, const char *label) {
if (!data || size <= kHeaderSize || !bulkOutPipe) {
return kIOReturnBadArgument;
}

UInt8 hdrBuf[kHeaderSize];
memcpy(hdrBuf, data, kHeaderSize);

StandardUSB::DeviceRequest req{};
req.bmRequestType = StandardUSB::kDeviceRequestDirectionOut |
StandardUSB::kDeviceRequestTypeVendor |
StandardUSB::kDeviceRequestRecipientDevice;
req.bRequest      = kQcaPatchUpdate;
req.wValue        = 0;
req.wIndex        = 0;
req.wLength       = kHeaderSize;

uint32_t transferred = 0;
IOReturn ret = dev->deviceRequest(this, req, hdrBuf, transferred, kControlTimeoutMs);
if (ret != kIOReturnSuccess || transferred != kHeaderSize) {
IOLog("QcaBtMac: %s header failed (err=0x%08x, sent=%u)\n", label, ret, transferred);
return (ret == kIOReturnSuccess) ? kIOReturnUnderrun : ret;
}

IOBufferMemoryDescriptor *memDesc =
IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, kIODirectionOut, kMaxChunkSize);
if (!memDesc) {
return kIOReturnNoMemory;
}

if (memDesc->prepare() != kIOReturnSuccess) {
memDesc->release();
return kIOReturnNoMemory;
}

UInt8 *bulkBuf = static_cast<UInt8 *>(memDesc->getBytesNoCopy());
UInt32 offset = kHeaderSize;

while (offset < size) {
UInt32 chunkLen = size - offset;
if (chunkLen > kMaxChunkSize) {
chunkLen = kMaxChunkSize;
}
memcpy(bulkBuf, data + offset, chunkLen);

transferred = 0;
ret = bulkOutPipe->io(memDesc, chunkLen, transferred, kControlTimeoutMs);
if (ret != kIOReturnSuccess || transferred != chunkLen) {
IOLog("QcaBtMac: %s bulk chunk failed at %u/%u (err=0x%08x, sent=%u)\n",
label, offset, size, ret, transferred);
if (ret == kIOReturnSuccess) {
ret = kIOReturnUnderrun;
}
break;
}
offset += chunkLen;
}

memDesc->complete();
memDesc->release();

if (ret == kIOReturnSuccess) {
IOSleep(20);
IOLog("QcaBtMac: uploaded %s (%u bytes)\n", label, size);
}
return ret;
}

bool QcaBtUSBDevice::start(IOService *provider) {
if (!super::start(provider)) {
return false;
}

auto *dev = OSDynamicCast(IOUSBHostDevice, provider);
if (!dev) {
return false;
}

char diag[160];

if (!dev->open(this)) {
IOLog("QcaBtMac: failed to open IOUSBHostDevice\n");
dev->setProperty("QcaBtMacStage", "open_failed");
return false;
}

IOReturn cfgPreRet = kIOReturnSuccess;
if (!dev->getConfigurationDescriptor()) {
cfgPreRet = dev->setConfiguration(1, false);
IOLog("QcaBtMac: initial setConfiguration(1, false) -> 0x%08x\n", cfgPreRet);
IOSleep(10);
}

QcaVersion ver{};
IOReturn ret = kIOReturnError;
for (int attempt = 0; attempt < 3; ++attempt) {
ret = sendVendorRead(dev, kQcaGetTargetVersion, &ver, sizeof(ver));
if (ret == kIOReturnSuccess) {
break;
}
IOSleep(20);
}

if (ret != kIOReturnSuccess) {
snprintf(diag, sizeof(diag), "getTargetVer_err=0x%08x_cfgPre=0x%08x", ret, cfgPreRet);
dev->setProperty("QcaBtMacStage", diag);
dev->close(this);
return false;
}

IOLog("QcaBtMac: target rom=0x%08x build=0x%08x\n", ver.romVersion, ver.buildVersion);
dev->setProperty("QcaBtMacRomVersion", ver.romVersion, 32);
dev->setProperty("QcaBtMacBuildVersion", ver.buildVersion, 32);

UInt8 preStatus = 0;
ret = sendVendorRead(dev, kQcaCheckStatus, &preStatus, sizeof(preStatus));
if (ret != kIOReturnSuccess) {
snprintf(diag, sizeof(diag), "checkStatus_err=0x%08x", ret);
dev->setProperty("QcaBtMacStage", diag);
dev->close(this);
return false;
}
dev->setProperty("QcaBtMacPreStatus", preStatus, 32);

UInt8 postStatus = preStatus;
IOReturn hciResetRet = kIOReturnSuccess;
QcaVersion postVer = ver;

if ((preStatus & (kQcaPatchUpdated | kQcaSysCfgUpdated)) != (kQcaPatchUpdated | kQcaSysCfgUpdated)) {
const auto *profile = QcaBtPayload::selectProfile(ver.romVersion);
if (!profile) {
snprintf(diag, sizeof(diag), "no_profile_rom=0x%08x", ver.romVersion);
dev->setProperty("QcaBtMacStage", diag);
dev->close(this);
return false;
}

IOUSBHostInterface *iface0 = nullptr;
if (OSIterator *it = dev->getChildIterator(gIOServicePlane)) {
OSObject *obj = nullptr;
while ((obj = it->getNextObject()) != nullptr) {
auto *cand = OSDynamicCast(IOUSBHostInterface, obj);
if (cand && cand->getInterfaceDescriptor() &&
cand->getInterfaceDescriptor()->bInterfaceNumber == 0) {
iface0 = cand;
iface0->retain();
break;
}
}
it->release();
}

if (!iface0 || !iface0->open(this)) {
if (iface0) {
iface0->release();
}
dev->setProperty("QcaBtMacStage", "iface0_open_failed");
dev->close(this);
return false;
}

uint8_t bulkOutAddr = 0x02;
const StandardUSB::EndpointDescriptor *ep = nullptr;
while ((ep = StandardUSB::getNextEndpointDescriptor(
iface0->getConfigurationDescriptor(),
iface0->getInterfaceDescriptor(),
ep)) != nullptr) {
if ((ep->bEndpointAddress & 0x80) == 0 && (ep->bmAttributes & 0x03) == 0x02) {
bulkOutAddr = ep->bEndpointAddress;
break;
}
}

IOUSBHostPipe *bulkOutPipe = iface0->copyPipe(bulkOutAddr);
if (!bulkOutPipe) {
iface0->close(this);
iface0->release();
dev->setProperty("QcaBtMacStage", "copyPipe_0x02_failed");
dev->close(this);
return false;
}

if (!(preStatus & kQcaPatchUpdated)) {
ret = downloadBlob(dev, bulkOutPipe, profile->rampatch, profile->rampatchSize, "rampatch");
if (ret != kIOReturnSuccess) {
snprintf(diag, sizeof(diag), "rampatch_bulk_err=0x%08x", ret);
dev->setProperty("QcaBtMacStage", diag);
bulkOutPipe->release();
iface0->close(this);
iface0->release();
dev->close(this);
return false;
}
}

if (!(preStatus & kQcaSysCfgUpdated)) {
ret = downloadBlob(dev, bulkOutPipe, profile->nvm, profile->nvmSize, "nvm");
if (ret != kIOReturnSuccess) {
snprintf(diag, sizeof(diag), "nvm_bulk_err=0x%08x", ret);
dev->setProperty("QcaBtMacStage", diag);
bulkOutPipe->release();
iface0->close(this);
iface0->release();
dev->close(this);
return false;
}
}

bulkOutPipe->release();
iface0->close(this);
iface0->release();

// Issue standard Bluetooth HCI_Reset (0x0C03) over EP0 Class Control OUT to boot RAM firmware.
UInt8 hciResetCmd[3] = { 0x03, 0x0c, 0x00 };
StandardUSB::DeviceRequest hciReq{};
hciReq.bmRequestType = StandardUSB::kDeviceRequestDirectionOut |
StandardUSB::kDeviceRequestTypeClass |
StandardUSB::kDeviceRequestRecipientDevice;
hciReq.bRequest      = 0x00;
hciReq.wValue        = 0;
hciReq.wIndex        = 0;
hciReq.wLength       = sizeof(hciResetCmd);

uint32_t sent = 0;
hciResetRet = dev->deviceRequest(this, hciReq, hciResetCmd, sent, kControlTimeoutMs);
IOLog("QcaBtMac: HCI_Reset returned 0x%08x (sent=%u)\n", hciResetRet, sent);
IOSleep(150);

postStatus = 0;
if (sendVendorRead(dev, kQcaCheckStatus, &postStatus, sizeof(postStatus)) == kIOReturnSuccess) {
IOLog("QcaBtMac: post-reset status=0x%02x\n", postStatus);
}
sendVendorRead(dev, kQcaGetTargetVersion, &postVer, sizeof(postVer));
} else {
IOLog("QcaBtMac: firmware already active (status=0x%02x)\n", preStatus);
}

dev->setProperty("QcaBtMacPostStatus", postStatus, 32);
dev->setProperty("QcaBtMacPostBuild", postVer.buildVersion, 32);

snprintf(diag, sizeof(diag), "ok_rom=0x%x_pre=0x%x_post=0x%x_hci=0x%x_bld=0x%x",
ver.romVersion, preStatus, postStatus, hciResetRet, postVer.buildVersion);
dev->setProperty("QcaBtMacStage", diag);

// Release exclusive device ownership BEFORE registering child interfaces for bluetoothd.
dev->close(this);

if (OSIterator *it = dev->getChildIterator(gIOServicePlane)) {
OSObject *obj = nullptr;
while ((obj = it->getNextObject()) != nullptr) {
auto *iface = OSDynamicCast(IOUSBHostInterface, obj);
if (iface && !(iface->getState() & kIOServiceRegisteredState)) {
iface->registerService();
}
}
it->release();
}

return false;
}
