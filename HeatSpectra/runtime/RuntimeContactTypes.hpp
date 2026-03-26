#pragma once

#include "contact/ContactTypes.hpp"
#include "domain/ContactData.hpp"

#include <vector>

struct RuntimeContactBinding {
    ContactPairData contactPair{};
    ContactPairPayloadConfig payloadPair{};
    uint32_t emitterRuntimeModelId = 0;
    uint32_t receiverRuntimeModelId = 0;
};

struct RuntimeContactResult {
    RuntimeContactBinding binding{};
    std::vector<ContactPair> contactPairsCPU;
};
