#pragma once

#include <cstdint>

enum class ContactCouplingKind : uint32_t {
    SourceToReceiver = 0,
    ReceiverToReceiver = 1
};

struct ConfiguredContactPair {
    ContactCouplingKind kind = ContactCouplingKind::SourceToReceiver;
    uint32_t emitterModelId = 0;
    uint32_t receiverModelId = 0;
    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;

    bool operator==(const ConfiguredContactPair& rhs) const {
        return kind == rhs.kind &&
            emitterModelId == rhs.emitterModelId &&
            receiverModelId == rhs.receiverModelId &&
            minNormalDot == rhs.minNormalDot &&
            contactRadius == rhs.contactRadius;
    }

    bool operator!=(const ConfiguredContactPair& rhs) const {
        return !(*this == rhs);
    }
};
