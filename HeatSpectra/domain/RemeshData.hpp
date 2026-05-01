#pragma once

#include "domain/GeometryData.hpp"

#include <cstdint>
#include <vector>

//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects, 
//                                                          backend/controller objects or GPU resources 
//                                                        - They must not be used directly by any backends ]

struct RemeshData {
    uint64_t payloadHash = 0;
    NodeDataHandle sourceMeshHandle{};
    uint64_t sourcePayloadHash = 0;
    int iterations = 1;
    float minAngleDegrees = 20.0f;
    float maxEdgeLength = 0.1f;
    float stepSize = 0.25f;
    bool active = false;

    void sealPayload();
};
