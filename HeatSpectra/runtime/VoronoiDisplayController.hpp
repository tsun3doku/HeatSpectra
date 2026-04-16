#pragma once

#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace render {
class VoronoiOverlayRenderer;
}

class VoronoiDisplayController {
public:
    struct Config {
        bool showVoronoi = false;
        bool showPoints = false;
        uint32_t nodeCount = 0;
        const VoronoiNode* mappedVoronoiNodes = nullptr;
        VkBuffer nodeBuffer = VK_NULL_HANDLE;
        VkDeviceSize nodeBufferOffset = 0;
        VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedPositionBufferOffset = 0;
        VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
        VkDeviceSize neighborIndicesBufferOffset = 0;
        VkBuffer occupancyPointBuffer = VK_NULL_HANDLE;
        VkDeviceSize occupancyPointBufferOffset = 0;
        uint32_t occupancyPointCount = 0;
        std::vector<VoronoiSurfaceProduct> surfaces;
        uint64_t contentHash = 0;

        bool anyVisible() const {
            return showVoronoi || showPoints;
        }

        bool isValid() const {
            return nodeCount != 0 &&
                mappedVoronoiNodes != nullptr &&
                nodeBuffer != VK_NULL_HANDLE &&
                seedPositionBuffer != VK_NULL_HANDLE &&
                neighborIndicesBuffer != VK_NULL_HANDLE;
        }

        bool operator==(const Config& other) const {
            return showVoronoi == other.showVoronoi &&
                showPoints == other.showPoints &&
                nodeCount == other.nodeCount &&
                mappedVoronoiNodes == other.mappedVoronoiNodes &&
                nodeBuffer == other.nodeBuffer &&
                nodeBufferOffset == other.nodeBufferOffset &&
                seedPositionBuffer == other.seedPositionBuffer &&
                seedPositionBufferOffset == other.seedPositionBufferOffset &&
                neighborIndicesBuffer == other.neighborIndicesBuffer &&
                neighborIndicesBufferOffset == other.neighborIndicesBufferOffset &&
                occupancyPointBuffer == other.occupancyPointBuffer &&
                occupancyPointBufferOffset == other.occupancyPointBufferOffset &&
                occupancyPointCount == other.occupancyPointCount &&
                surfaces == other.surfaces &&
                contentHash == other.contentHash;
        }
    };

    void setOverlayRenderer(render::VoronoiOverlayRenderer* updatedOverlayRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    render::VoronoiOverlayRenderer* overlayRenderer = nullptr;
    std::unordered_map<uint64_t, Config> activeConfigsBySocket;
    std::unordered_set<uint64_t> touchedSocketKeys;
};

inline uint64_t computeContentHash(const VoronoiDisplayController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showVoronoi ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showPoints ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, config.nodeCount);
    if (config.nodeCount != 0 && config.mappedVoronoiNodes != nullptr) {
        hash = RuntimeProductHash::mixBytes(
            hash,
            config.mappedVoronoiNodes,
            sizeof(VoronoiNode) * config.nodeCount);
    }
    hash = RuntimeProductHash::mixPod(hash, config.nodeBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.nodeBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.seedPositionBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.seedPositionBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.neighborIndicesBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.neighborIndicesBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.occupancyPointBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.occupancyPointBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.occupancyPointCount);
    hash = RuntimeProductHash::mix(hash, static_cast<uint64_t>(config.surfaces.size()));
    for (const VoronoiSurfaceProduct& surfaceProduct : config.surfaces) {
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.runtimeModelId);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.nodeOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.nodeCount);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.surfaceMappingBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.surfaceMappingBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.vertexBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.vertexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.indexBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.indexBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.indexCount);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.modelMatrix);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.intrinsicVertexCount);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.candidateBuffer);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.candidateBufferOffset);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.supportingHalfedgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.supportingAngleView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.halfedgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.edgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.triangleView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.lengthView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputHalfedgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputEdgeView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputTriangleView);
        hash = RuntimeProductHash::mixPod(hash, surfaceProduct.inputLengthView);
        hash = RuntimeProductHash::mixPodVector(hash, surfaceProduct.surfaceCellIndices);
        hash = RuntimeProductHash::mixPodVector(hash, surfaceProduct.seedFlags);
    }
    return hash;
}
