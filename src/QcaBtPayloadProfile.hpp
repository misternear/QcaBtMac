#pragma once
#include <IOKit/IOTypes.h>

namespace QcaBtPayload {

struct FirmwareProfile {
    UInt32 romVersion = 0;
    const UInt8 *rampatch = nullptr;
    UInt32 rampatchSize = 0;
    const UInt8 *nvm = nullptr;
    UInt32 nvmSize = 0;
};

const FirmwareProfile *selectProfile(UInt32 romVersion);

} // namespace QcaBtPayload
