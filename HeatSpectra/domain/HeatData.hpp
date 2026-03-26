#pragma once

#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "heat/HeatSystemPresets.hpp"

#include <cstddef>
#include <vector>

struct HeatSourceData {
    NodeDataHandle geometryHandle{};
    float temperature = 100.0f;
};

struct HeatReceiverData {
    NodeDataHandle geometryHandle{};
};

struct HeatData {
    std::vector<NodeDataHandle> sourceHandles;
    std::vector<NodeDataHandle> receiverGeometryHandles;
    std::vector<HeatMaterialBindingEntry> materialBindings;
    bool active = false;
    bool paused = false;
    bool resetRequested = false;

    std::size_t size() const {
        return receiverGeometryHandles.size();
    }
};
