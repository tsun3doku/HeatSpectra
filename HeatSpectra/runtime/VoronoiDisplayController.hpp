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
        const voronoi::Node* mappedVoronoiNodes = nullptr;
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
        uint64_t displayHash = 0;

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

inline uint64_t buildDisplayHash(const VoronoiDisplayController::Config& config, uint64_t productHash) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showVoronoi ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showPoints ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, productHash);
    return hash;
}
