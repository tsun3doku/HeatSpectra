#pragma once

#include <cstdint>
#include "util/Structs.hpp"

enum class ContactCouplingType : uint32_t {
    SourceToReceiver = 0,
    ReceiverToReceiver = 1
};

struct ContactPair {
    ContactSampleGPU samples[7];
    float contactArea;
};
