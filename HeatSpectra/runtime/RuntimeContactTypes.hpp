#pragma once

#include "contact/ContactTypes.hpp"
#include "domain/ContactData.hpp"
#include "domain/GeometryData.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"

#include <vector>

struct RuntimeContactGeometry {
    GeometryData geometry{};
    SupportingHalfedge::IntrinsicMesh intrinsicMesh;
};

struct RuntimeContactPairConfig {
    ContactCouplingType couplingType = ContactCouplingType::SourceToReceiver;
    RuntimeContactGeometry emitter{};
    RuntimeContactGeometry receiver{};
    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;
};

struct RuntimeContactBinding {
    ContactPairData contactPair{};
    RuntimeContactPairConfig runtimePair{};
    uint32_t emitterRuntimeModelId = 0;
    uint32_t receiverRuntimeModelId = 0;
    std::vector<uint32_t> receiverTriangleIndices;
};

struct RuntimeContactResult {
    RuntimeContactBinding binding{};
    std::vector<ContactPair> contactPairsCPU;
};
