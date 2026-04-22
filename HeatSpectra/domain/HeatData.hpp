#pragma once

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "heat/HeatSystemPresets.hpp"

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
    uint64_t meshPayloadHash = 0;
    float temperature = 100.0f;

    void sealPayload();
};

struct HeatReceiverData {
    uint64_t payloadHash = 0;
    NodeDataHandle meshHandle{};
    uint64_t meshPayloadHash = 0;

    void sealPayload();
};

struct HeatMaterialBinding {
    uint32_t receiverModelNodeId = 0;
    HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
};

struct HeatData {
    uint64_t payloadHash = 0;
    uint64_t voronoiPayloadHash = 0;
    uint64_t contactPayloadHash = 0;
    std::vector<NodeDataHandle> sourceHandles;
    std::vector<NodeDataHandle> receiverMeshHandles;
    std::vector<HeatMaterialBinding> materialBindings;
    bool active = false;
    bool paused = false;
    bool resetRequested = false;

    void sealPayload();
};
