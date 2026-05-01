#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "contact/ContactGpuStructs.hpp"

enum class ContactCouplingType : uint32_t {
    SourceToReceiver = 0,
    ReceiverToReceiver = 1
};

struct ContactPair {
    contact::Sample samples[7];
    float contactArea;
};

struct ContactLineVertex {
    glm::vec3 position;
    glm::vec3 color;

    bool operator==(const ContactLineVertex& other) const {
        return position.x == other.position.x &&
            position.y == other.position.y &&
            position.z == other.position.z &&
            color.x == other.color.x &&
            color.y == other.color.y &&
            color.z == other.color.z;
    }
};

struct ContactCoupling {
    ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
    uint32_t emitterRuntimeModelId = 0;
    uint32_t receiverRuntimeModelId = 0;
    std::vector<uint32_t> receiverTriangleIndices;
    const ContactPair* mappedContactPairs = nullptr;
    uint32_t contactPairCount = 0;

    bool isValid() const {
        return emitterRuntimeModelId != 0 &&
            receiverRuntimeModelId != 0 &&
            !receiverTriangleIndices.empty() &&
            mappedContactPairs != nullptr &&
            contactPairCount != 0;
    }

    bool operator==(const ContactCoupling& other) const {
        return couplingType == other.couplingType &&
            emitterRuntimeModelId == other.emitterRuntimeModelId &&
            receiverRuntimeModelId == other.receiverRuntimeModelId &&
            receiverTriangleIndices == other.receiverTriangleIndices &&
            mappedContactPairs == other.mappedContactPairs &&
            contactPairCount == other.contactPairCount;
    }
};
