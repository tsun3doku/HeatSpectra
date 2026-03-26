#pragma once

#include <cstdint>
#include <vector>

struct HeatReceiverIntrinsicMeshInput {
    uint64_t geometryRevision = 0;
    uint64_t revisionHash = 0;
    std::vector<float> pointPositions;
    std::vector<uint32_t> triangleIndices;
};

