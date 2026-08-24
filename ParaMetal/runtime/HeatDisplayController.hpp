#pragma once

#include "hash/HashBuilder.hpp"
#include "heat/HeatGpuStructs.hpp"

#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>

namespace render {
class HeatOverlayRenderer;
}

class HeatDisplayController {
public:
    struct Config {
        bool showHeatOverlay = false;
        bool showFluxVectors = false;
        bool showHeatPalette = false;
        bool showContactLevelSet = false;
        float fluxVectorScale = 1.0f;
        float contactLevelSetRange = 1.0f;
        bool authoredActive = false;
        bool active = false;
        std::vector<uint32_t> modelRuntimeIds;
        std::vector<VkBuffer> modelRenderVertexBuffers;
        std::vector<VkDeviceSize> modelRenderVertexBufferOffsets;
        std::vector<VkBuffer> modelRenderIndexBuffers;
        std::vector<VkDeviceSize> modelRenderIndexBufferOffsets;
        std::vector<uint32_t> modelRenderIndexCounts;
        std::vector<glm::mat4> modelMatrices;
        float canonicalToWorldScale = 1.0f;
        std::vector<float> modelInitialTemperaturesC;
        std::vector<float> modelBoundaryTemperaturesC;
        std::vector<uint32_t> modelBoundaryConditionTypes;
        std::vector<VkBuffer> modelSurfaceBuffers;
        std::vector<VkDeviceSize> modelSurfaceBufferOffsets;
        std::vector<uint32_t> modelSurfacePointCounts;
        std::vector<std::array<VkBufferView, 11>> modelBufferViews;
        std::vector<VkBuffer> modelSurfaceGradientBuffers;
        std::vector<VkDeviceSize> modelSurfaceGradientBufferOffsets;

        glm::vec3 globalSdfGridMin{0.0f};
        glm::ivec3 globalSdfGridDim{0};
        float globalSdfCellSize = 0.0f;
        std::vector<VkImageView> globalSdfImageViews;
        std::vector<VkImageView> globalPsiImageViews;
        VkSampler globalSdfSampler = VK_NULL_HANDLE;
        VkBuffer contactRegionBuffer = VK_NULL_HANDLE;
        VkDeviceSize contactRegionBufferOffset = 0;
        VkDeviceSize contactRegionBufferSize = 0;
        VkBuffer indirectDrawBuffer = VK_NULL_HANDLE;
        VkDeviceSize indirectDrawBufferOffset = 0;

        uint64_t displayHash = 0;

        bool anyVisible() const {
            return showHeatOverlay || showFluxVectors || showHeatPalette || showContactLevelSet;
        }

        bool isValid() const {
            const size_t modelCount = modelRuntimeIds.size();
            return modelCount != 0 &&
                modelCount == modelRenderVertexBuffers.size() &&
                modelCount == modelRenderVertexBufferOffsets.size() &&
                modelCount == modelRenderIndexBuffers.size() &&
                modelCount == modelRenderIndexBufferOffsets.size() &&
                modelCount == modelRenderIndexCounts.size() &&
                modelCount == modelMatrices.size() &&
                modelCount == modelInitialTemperaturesC.size() &&
                modelCount == modelBoundaryTemperaturesC.size() &&
                modelCount == modelBoundaryConditionTypes.size() &&
                modelCount == modelSurfaceBuffers.size() &&
                modelCount == modelSurfaceBufferOffsets.size() &&
                modelCount == modelSurfacePointCounts.size() &&
                modelCount == modelBufferViews.size() &&
                modelCount == modelSurfaceGradientBuffers.size() &&
                modelCount == modelSurfaceGradientBufferOffsets.size();
        }

    };

    void setOverlayRenderer(render::HeatOverlayRenderer* updatedOverlayRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    render::HeatOverlayRenderer* overlayRenderer = nullptr;
    std::unordered_map<uint64_t, Config> configsBySocket;
    std::unordered_set<uint64_t> syncedSockets;
};

inline uint64_t buildDisplayHash(const HeatDisplayController::Config& config, uint64_t productDisplayHash) {
    uint64_t hash = HashBuilder::start();
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.showHeatOverlay ? 1u : 0u));
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.showFluxVectors ? 1u : 0u));
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.showHeatPalette ? 1u : 0u));
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.showContactLevelSet ? 1u : 0u));
    HashBuilder::combinePod(hash, config.fluxVectorScale);
    HashBuilder::combinePod(hash, config.contactLevelSetRange);
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.authoredActive ? 1u : 0u));
    HashBuilder::combinePod(hash, static_cast<uint64_t>(config.active ? 1u : 0u));
    HashBuilder::combinePod(hash, config.canonicalToWorldScale);
    HashBuilder::combine(hash, productDisplayHash);
    return hash;
}
