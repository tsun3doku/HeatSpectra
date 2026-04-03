#pragma once

#include "domain/GeometryData.hpp"
#include "domain/RemeshParams.hpp"

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
    RemeshParams params{};
    bool active = false;
};
