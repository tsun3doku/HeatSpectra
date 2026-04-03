#pragma once

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "heat/HeatSystemPresets.hpp"

#include <cstddef>
#include <vector>

//                                                      [ Invariant:
//                                                        - Payloads are node graph authored data
//                                                        - They may contain authored values and NodeDataHandle values
//                                                        - They must not contain runtime objects/ids, scene objects, 
//                                                          backend/controller objects or GPU resources 
//                                                        - They must not be used directly by any backends ]

struct HeatSourceData {
    uint64_t payloadHash = 0;
    NodeDataHandle meshHandle{};
    float temperature = 100.0f;
};

struct HeatReceiverData {
    uint64_t payloadHash = 0;
    NodeDataHandle meshHandle{};
};

struct HeatData {
    uint64_t payloadHash = 0;
    std::vector<NodeDataHandle> sourceHandles;
    std::vector<NodeDataHandle> receiverMeshHandles;
    std::vector<HeatMaterialBindingEntry> materialBindings;
    bool active = false;
    bool paused = false;
    bool resetRequested = false;

    std::size_t size() const {
        return receiverMeshHandles.size();
    }
};