#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "spatial/VoxelGrid.hpp"
#include "voronoi/VoronoiIntegrator.hpp"
#include "voronoi/VoronoiSeeder.hpp"

class VoronoiModelRuntime;

struct VoronoiDomain {
    uint32_t receiverModelId = 0;
    VoronoiModelRuntime* modelRuntime = nullptr;
    std::unique_ptr<VoronoiSeeder> seeder;
    std::unique_ptr<VoronoiIntegrator> integrator;
    std::vector<uint32_t> seedFlags;
    VoxelGrid voxelGrid;
    bool voxelGridBuilt = false;
    uint32_t nodeOffset = 0;
    uint32_t nodeCount = 0;
};
