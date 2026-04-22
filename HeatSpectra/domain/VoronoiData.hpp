#pragma once

#include "domain/VoronoiParams.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"

#include <vector>

//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects, 
//                                                          backend/controller objects, or GPU resources ]

struct VoronoiData {
    uint64_t payloadHash = 0;
    VoronoiParams params{};
    std::vector<NodeDataHandle> receiverMeshHandles;
    std::vector<uint64_t> receiverPayloadHashes;
    bool active = false;

    void sealPayload();
};
