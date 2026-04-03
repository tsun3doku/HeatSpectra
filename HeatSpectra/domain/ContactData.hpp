#pragma once

#include "contact/ContactTypes.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"

#include <cstdint>
#include <vector>

//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects, 
//                                                          backend/controller objects or GPU resources 
//                                                        - They must not be used directly by any backends ]

enum class ContactPairRole : uint8_t {
    Source = 0,
    Receiver = 1
};

struct ContactPairEndpoint {
    ContactPairRole role = ContactPairRole::Receiver;
    NodeDataHandle payloadHandle{};
    NodeDataHandle meshHandle{};
};

struct ContactPairData {
    bool hasValidContact = false;
    ContactCouplingType kind = ContactCouplingType::SourceToReceiver;
    ContactPairEndpoint endpointA{};
    ContactPairEndpoint endpointB{};

    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;
    NodeDataHandle contactPairsHandle{};
};

struct ContactData {
    uint64_t payloadHash = 0;
    ContactPairData pair{};
    bool active = false;

    std::size_t size() const {
        return (active && pair.hasValidContact) ? 1u : 0u;
    }
};
