#pragma once

#include "contact/ContactTypes.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"

#include <cstdint>
//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects, 
//                                                          backend/controller objects or GPU resources 
//                                                        - They must not be used directly by any backends ]

struct ContactPairEndpoint {
    NodeDataHandle payloadHandle{};
    NodeDataHandle meshHandle{};
};

struct ContactPairData {
    bool hasValidContact = false;
    ContactCouplingType type = ContactCouplingType::SourceToReceiver;
    ContactPairEndpoint endpointA{};
    ContactPairEndpoint endpointB{};

    float minNormalDot = -0.65f;
    float contactRadius = 0.01f;
    NodeDataHandle contactPairsHandle{};
};

struct ContactData {
    uint64_t payloadHash = 0;
    uint64_t emitterPayloadHash = 0;
    uint64_t receiverPayloadHash = 0;
    ContactPairData pair{};
    bool active = false;

    void sealPayload();
};
