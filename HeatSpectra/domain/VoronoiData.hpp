#pragma once

#include "domain/VoronoiParams.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"

#include <cstddef>
#include <vector>

struct VoronoiData {
    VoronoiParams params{};
    std::vector<NodeDataHandle> receiverGeometryHandles;
    bool active = false;

    std::size_t size() const {
        return receiverGeometryHandles.size();
    }
};
