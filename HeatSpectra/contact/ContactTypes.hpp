#pragma once

#include "domain/GeometryData.hpp"
#include "domain/RemeshData.hpp"

#include <cstdint>
#include "util/Structs.hpp"

enum class ContactCouplingType : uint32_t {
    SourceToReceiver = 0,
    ReceiverToReceiver = 1
};

struct ContactGeometryPayload {
    NodeDataHandle geometryHandle{};
    GeometryData geometry{};
    IntrinsicMeshData intrinsic{};
};

struct ContactPairPayloadConfig {
    ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
    ContactGeometryPayload emitter{};
    ContactGeometryPayload receiver{};
    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;
};

struct ContactPair {
    ContactSampleGPU samples[7];
    float contactArea;
};
