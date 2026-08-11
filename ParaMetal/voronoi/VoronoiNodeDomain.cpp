#include "VoronoiNodeDomain.hpp"

#include "spatial/VoxelGrid.hpp"
#include "util/GMLS.hpp"

#include <algorithm>
#include <cmath>

void VoronoiNodeDomain::rebuild(
    const std::vector<uint32_t>& nodeFlags,
    const std::vector<glm::vec4>& candidatePositions,
    const std::vector<voronoi::Node>& candidateNodes,
    const std::vector<voronoi::NodeCoupling>& candidateCouplings,
    const std::vector<float>& candidateSurfacePatchAreas) {
    nodes.clear();
    couplings.clear();
    surfacePatchAreas.clear();
    surfaceNodeIds.clear();
    surfaceStencils.clear();
    surfaceValueWeights.clear();
    surfaceGradientWeights.clear();

    if (nodeFlags.size() != candidateNodes.size() ||
        candidatePositions.size() != candidateNodes.size() ||
        candidateSurfacePatchAreas.size() != candidateNodes.size()) {
        return;
    }

    std::vector<uint32_t> candidateToNodeId(candidateNodes.size(), InvalidNodeId);
    std::vector<uint32_t> nodeToCandidateId;
    nodeToCandidateId.reserve(candidateNodes.size());

    std::vector<glm::vec3> compactPositions;
    compactPositions.reserve(candidateNodes.size());

    for (uint32_t candidateId = 0; candidateId < static_cast<uint32_t>(candidateNodes.size()); ++candidateId) {
        if ((nodeFlags[candidateId] & voronoi::NodeFlags::Ghost) != 0u) {
            continue;
        }
        if (std::abs(candidateNodes[candidateId].volume) <= MinimumNodeVolume) {
            continue;
        }

        const uint32_t nodeId = static_cast<uint32_t>(nodeToCandidateId.size());
        candidateToNodeId[candidateId] = nodeId;
        nodeToCandidateId.push_back(candidateId);
        const float surfacePatchArea = candidateSurfacePatchAreas[candidateId];
        if (surfacePatchArea > 0.0f) {
            surfaceNodeIds.push_back(nodeId);
        }
        compactPositions.push_back(glm::vec3(candidatePositions[candidateId]));
    }

    nodes.resize(nodeToCandidateId.size());
    surfacePatchAreas.resize(nodeToCandidateId.size(), 0.0f);
    couplings.reserve(candidateCouplings.size());
    for (uint32_t nodeId = 0; nodeId < static_cast<uint32_t>(nodeToCandidateId.size()); ++nodeId) {
        const uint32_t candidateId = nodeToCandidateId[nodeId];
        const voronoi::Node& candidate = candidateNodes[candidateId];
        voronoi::Node compact{};
        compact.volume = std::abs(candidate.volume);
        compact.neighborOffset = static_cast<uint32_t>(couplings.size());
        compact.neighborCount = 0;
        compact.interfaceNeighborCount = 0;
        surfacePatchAreas[nodeId] = std::max(candidateSurfacePatchAreas[candidateId], 0.0f);

        const size_t begin = candidate.neighborOffset;
        const size_t end = begin + candidate.neighborCount;
        for (size_t interfaceId = begin; interfaceId < end && interfaceId < candidateCouplings.size(); ++interfaceId) {
            const voronoi::NodeCoupling& candidateCoupling = candidateCouplings[interfaceId];
            if (candidateCoupling.neighborNodeId >= candidateToNodeId.size()) {
                continue;
            }
            const uint32_t neighborNodeId = candidateToNodeId[candidateCoupling.neighborNodeId];
            if (neighborNodeId == InvalidNodeId) {
                continue;
            }
            couplings.push_back({neighborNodeId, candidateCoupling.conductance});
        }
        compact.neighborCount = static_cast<uint32_t>(couplings.size()) - compact.neighborOffset;
        compact.interfaceNeighborCount = compact.neighborCount;
        nodes[nodeId] = compact;
    }

    nodeIndex.rebuild(compactPositions);
}

bool VoronoiNodeDomain::buildSurfaceMappings(
    const std::vector<glm::vec3>& surfacePoints,
    const std::vector<glm::vec3>& surfaceNormals,
    const VoxelGrid& voxelGrid) {
    surfaceStencils.assign(surfacePoints.size(), {});
    surfaceValueWeights.clear();
    surfaceGradientWeights.clear();

    if (!nodeIndex.isValid() || surfacePoints.size() != surfaceNormals.size()) {
        return false;
    }

    constexpr uint32_t maximumSupportCount = 32;

    for (uint32_t vertexId = 0; vertexId < static_cast<uint32_t>(surfacePoints.size()); ++vertexId) {
        std::vector<uint32_t> nearestNodeIds;
        std::vector<float> distanceSquared;
        nodeIndex.findKNearest(surfacePoints[vertexId], maximumSupportCount, nearestNodeIds, distanceSquared);

        std::vector<uint32_t> sourceNodeIds;
        std::vector<glm::dvec3> sourcePositions;
        std::vector<double> valueWeights;
        std::vector<glm::dvec3> gradientWeights;
        float maximumDistanceSquared = 0.0f;
        for (uint32_t index = 0; index < static_cast<uint32_t>(nearestNodeIds.size()); ++index) {
            const uint32_t nodeId = nearestNodeIds[index];
            if (nodeId >= nodeIndex.getNodePositions().size()) {
                return false;
            }
            const glm::vec3& nodePosition = nodeIndex.getNodePositions()[nodeId];
            if (!voxelGrid.segmentStaysInside(surfacePoints[vertexId], nodePosition, 8)) {
                continue;
            }
            sourceNodeIds.push_back(nodeId);
            sourcePositions.push_back(glm::dvec3(nodePosition));
            maximumDistanceSquared = std::max(maximumDistanceSquared, distanceSquared[index]);
        }
        const double kernelRadius = std::max<double>(std::sqrt(maximumDistanceSquared) * 2.0, 1e-12);

        voronoi::GMLSSurfaceStencil& stencil = surfaceStencils[vertexId];
        if (sourceNodeIds.empty() ||
            !GMLS::computeSurfaceWeights(
                glm::dvec3(surfacePoints[vertexId]),
                glm::dvec3(surfaceNormals[vertexId]),
                sourcePositions,
                kernelRadius,
                valueWeights,
                gradientWeights)) {
            stencil.valueWeightOffset = 0;
            stencil.gradientWeightOffset = 0;
            stencil.valueWeightCount = 0;
            stencil.gradientWeightCount = 0;
            continue;
        }

        const uint32_t supportCount = static_cast<uint32_t>(sourceNodeIds.size());
        stencil.valueWeightOffset = static_cast<uint32_t>(surfaceValueWeights.size());
        stencil.gradientWeightOffset = static_cast<uint32_t>(surfaceGradientWeights.size());
        stencil.valueWeightCount = supportCount;
        stencil.gradientWeightCount = supportCount;
        for (uint32_t i = 0; i < static_cast<uint32_t>(sourceNodeIds.size()); ++i) {
            const float valueWeight = static_cast<float>(valueWeights[i]);
            const glm::dvec3 gradient = gradientWeights[i];
            const glm::vec3 floatGradient(gradient);
            surfaceValueWeights.push_back({sourceNodeIds[i], valueWeight});
            surfaceGradientWeights.push_back({sourceNodeIds[i], floatGradient.x, floatGradient.y, floatGradient.z});
        }
    }

    return true;
}
