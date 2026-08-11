#pragma once

#include "hash/HashBuilder.hpp"
#include "runtime/package/RuntimePackages.hpp"
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

        uint64_t bindingKey = 0;
        uint32_t runtimeModelId = 0;
        VkBuffer candidateBuffer = VK_NULL_HANDLE;
        VkDeviceSize candidateBufferOffset = 0;

        VkBufferView supportingHalfedgeView = VK_NULL_HANDLE;
        VkBufferView supportingAngleView = VK_NULL_HANDLE;
        VkBufferView halfedgeView = VK_NULL_HANDLE;
        VkBufferView edgeView = VK_NULL_HANDLE;
        VkBufferView triangleView = VK_NULL_HANDLE;
        VkBufferView lengthView = VK_NULL_HANDLE;
        VkBufferView inputHalfedgeView = VK_NULL_HANDLE;
        VkBufferView inputEdgeView = VK_NULL_HANDLE;
        VkBufferView inputTriangleView = VK_NULL_HANDLE;
        VkBufferView inputLengthView = VK_NULL_HANDLE;
        size_t intrinsicVertexCount = 0;

        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize vertexBufferOffset = 0;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceSize indexBufferOffset = 0;
        uint32_t indexCount = 0;
        glm::mat4 modelMatrix{1.0f};
        float canonicalToWorldScale = 1.0f;

        uint64_t displayHash = 0;

        bool anyVisible() const {
            return showVoronoi || showPoints;
        }

        bool isValid() const {
            return candidateNodeCount != 0 &&
                candidateNodeBuffer != VK_NULL_HANDLE &&
                seedPositionBuffer != VK_NULL_HANDLE &&
                candidateNeighborIndicesBuffer != VK_NULL_HANDLE &&
                runtimeModelId != 0;
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
    HashBuilder::combinePod(hash, config.modelMatrix);
    HashBuilder::combineFloat(hash, config.canonicalToWorldScale);
    HashBuilder::combine(hash, productDisplayHash);
    return hash;
}
