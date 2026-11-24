#pragma once
#include <IOKit/IOService.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>

class QcaBtUSBDevice : public IOService {
    OSDeclareDefaultStructors(QcaBtUSBDevice)

public:
    bool start(IOService *provider) override;

private:
    enum : UInt8 {
        kQcaPatchUpdate      = 0x01,
        kQcaCheckStatus      = 0x05,
        kQcaGetTargetVersion = 0x09,
    };

    enum : UInt8 {
        kQcaSysCfgUpdated = 0x40,
        kQcaPatchUpdated  = 0x80,
    };

    struct __attribute__((packed)) QcaVersion {
        UInt32 romVersion;
        UInt32 buildVersion;
    };

    static constexpr UInt32 kHeaderSize       = 4;
    static constexpr UInt32 kMaxChunkSize     = 4096;
    static constexpr UInt32 kControlTimeoutMs = 5000;

    IOReturn sendVendorRead(IOUSBHostDevice *dev, UInt8 request, void *buffer, UInt16 length);
    IOReturn downloadBlob(IOUSBHostDevice *dev, IOUSBHostPipe *bulkOutPipe,
                          const UInt8 *data, UInt32 size, const char *label);
};
