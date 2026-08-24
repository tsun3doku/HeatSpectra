#pragma once

#include "hash/HashBuilder.hpp"
#include "runtime/package/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace render {
class VoronoiOverlayRenderer;
}

class VoronoiDisplayController {
public:
    struct Config {
        bool showVoronoi = false;
        bool showPoints = false;
        uint32_t candidateNodeCount = 0;
        const voronoi::Node* mappedCandidateNodes = nullptr;
        VkBuffer candidateNodeBuffer = VK_NULL_HANDLE;
        VkDeviceSize candidateNodeBufferOffset = 0;
        VkBuffer seedPositionBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedPositionBufferOffset = 0;
        VkBuffer candidateNeighborIndicesBuffer = VK_NULL_HANDLE;
        VkDeviceSize candidateNeighborIndicesBufferOffset = 0;
        VkBuffer occupancyPointBuffer = VK_NULL_HANDLE;
        VkDeviceSize occupancyPointBufferOffset = 0;
        uint32_t occupancyPointCount = 0;

        std::vector<uint32_t> modelRuntimeIds;
        std::vector<VkBuffer> candidateBuffers;
        std::vector<VkDeviceSize> candidateBufferOffsets;
        std::vector<std::array<VkBufferView, 10>> modelBufferViews;
        std::vector<size_t> intrinsicVertexCounts;
        std::vector<VkBuffer> modelVertexBuffers;
        std::vector<VkDeviceSize> modelVertexBufferOffsets;
        std::vector<VkBuffer> modelIndexBuffers;
        std::vector<VkDeviceSize> modelIndexBufferOffsets;
        std::vector<uint32_t> modelIndexCounts;
        std::vector<glm::mat4> modelMatrices;
        std::vector<float> modelCanonicalToWorldScales;

        uint64_t displayHash = 0;

        bool anyVisible() const {
            return showVoronoi || showPoints;
        }

        bool isValid() const {
            const size_t modelCount = modelRuntimeIds.size();
            return candidateNodeCount != 0 &&
                candidateNodeBuffer != VK_NULL_HANDLE &&
                seedPositionBuffer != VK_NULL_HANDLE &&
                candidateNeighborIndicesBuffer != VK_NULL_HANDLE &&
                (!showVoronoi || (modelCount != 0 &&
                    modelCount == candidateBuffers.size() &&
                    modelCount == candidateBufferOffsets.size() &&
                    modelCount == modelBufferViews.size() &&
                    modelCount == intrinsicVertexCounts.size() &&
                    modelCount == modelVertexBuffers.size() &&
                    modelCount == modelVertexBufferOffsets.size() &&
                    modelCount == modelIndexBuffers.size() &&
                    modelCount == modelIndexBufferOffsets.size() &&
                    modelCount == modelIndexCounts.size() &&
                    modelCount == modelMatrices.size() &&
                    modelCount == modelCanonicalToWorldScales.size()));
        }

    };

    void setOverlayRenderer(render::VoronoiOverlayRenderer* updatedOverlayRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    render::VoronoiOverlayRenderer* overlayRenderer = nullptr;
    std::unordered_map<uint64_t, Config> configsBySocket;
    std::unordered_set<uint64_t> syncedSockets;
};

inline uint64_t buildDisplayHash(const VoronoiDisplayController::Config& config, uint64_t productDisplayHash) {
    uint64_t hash = HashBuilder::start();
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.showVoronoi ? 1u : 0u));
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.showPoints ? 1u : 0u));
    HashBuilder::combine(hash, static_cast<uint64_t>(config.modelRuntimeIds.size()));
    for (size_t i = 0; i < config.modelRuntimeIds.size(); ++i) {
        HashBuilder::combine(hash, config.modelRuntimeIds[i]);
        HashBuilder::combinePod(hash, config.modelMatrices[i]);
        HashBuilder::combineFloat(hash, config.modelCanonicalToWorldScales[i]);
        HashBuilder::combine(hash, config.modelIndexCounts[i]);
    }
    HashBuilder::combine(hash, productDisplayHash);
    return hash;
}
