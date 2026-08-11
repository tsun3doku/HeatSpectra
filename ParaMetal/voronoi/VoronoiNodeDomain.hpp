#pragma once

#include "VoronoiNodeIndex.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

class VoxelGrid;

class VoronoiNodeDomain {
public:
    void rebuild(
        const std::vector<uint32_t>& nodeFlags,
        const std::vector<glm::vec4>& candidatePositions,
        const std::vector<voronoi::Node>& candidateNodes,
        const std::vector<voronoi::NodeCoupling>& candidateCouplings,
        const std::vector<float>& candidateSurfacePatchAreas);

    bool buildSurfaceMappings(
        const std::vector<glm::vec3>& surfacePoints,
        const std::vector<glm::vec3>& surfaceNormals,
        const VoxelGrid& voxelGrid);

    uint32_t getNodeCount() const { return static_cast<uint32_t>(nodes.size()); }

    const std::vector<voronoi::Node>& getNodes() const { return nodes; }
    const std::vector<voronoi::NodeCoupling>& getCouplings() const { return couplings; }
    const std::vector<float>& getSurfacePatchAreas() const { return surfacePatchAreas; }
    const std::vector<uint32_t>& getSurfaceNodeIds() const { return surfaceNodeIds; }
    const VoronoiNodeIndex& getNodeIndex() const { return nodeIndex; }
    const std::vector<voronoi::GMLSSurfaceStencil>& getSurfaceStencils() const { return surfaceStencils; }
    const std::vector<voronoi::GMLSSurfaceWeight>& getSurfaceValueWeights() const { return surfaceValueWeights; }
    const std::vector<voronoi::GMLSSurfaceGradientWeight>& getSurfaceGradientWeights() const { return surfaceGradientWeights; }

private:
    static constexpr uint32_t InvalidNodeId = std::numeric_limits<uint32_t>::max();
    static constexpr float MinimumNodeVolume = 1e-12f;
    std::vector<voronoi::Node> nodes;
    std::vector<voronoi::NodeCoupling> couplings;
    std::vector<float> surfacePatchAreas;
    std::vector<uint32_t> surfaceNodeIds;
    VoronoiNodeIndex nodeIndex;
    std::vector<voronoi::GMLSSurfaceStencil> surfaceStencils;
    std::vector<voronoi::GMLSSurfaceWeight> surfaceValueWeights;
    std::vector<voronoi::GMLSSurfaceGradientWeight> surfaceGradientWeights;
};
