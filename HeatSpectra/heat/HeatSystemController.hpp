#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "domain/HeatData.hpp"
#include "HeatSystemPresets.hpp"
#include "HeatSystemResources.hpp"
#include "domain/RemeshData.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "runtime/RuntimeThermalTypes.hpp"

class VulkanDevice;
class MemoryAllocator;
class ModelRegistry;
class UniformBufferManager;
class CommandPool;
class RenderRuntime;
class HeatSystem;

class HeatSystemController {
public:
    struct Config {
        HeatData authored;
        std::vector<GeometryData> sourceGeometries;
        std::vector<SupportingHalfedge::IntrinsicMesh> sourceIntrinsicMeshes;
        std::vector<uint32_t> sourceRuntimeModelIds;
        std::vector<GeometryData> receiverGeometries;
        std::vector<SupportingHalfedge::IntrinsicMesh> receiverIntrinsicMeshes;
        std::vector<uint32_t> receiverRuntimeModelIds;
        std::vector<VkBufferView> supportingHalfedgeViews;
        std::vector<VkBufferView> supportingAngleViews;
        std::vector<VkBufferView> halfedgeViews;
        std::vector<VkBufferView> edgeViews;
        std::vector<VkBufferView> triangleViews;
        std::vector<VkBufferView> lengthViews;
        std::vector<VkBufferView> inputHalfedgeViews;
        std::vector<VkBufferView> inputEdgeViews;
        std::vector<VkBufferView> inputTriangleViews;
        std::vector<VkBufferView> inputLengthViews;
        std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;
        std::unordered_map<uint32_t, float> sourceTemperatureByRuntimeId;
        uint32_t voronoiNodeCount = 0;
        const VoronoiNode* voronoiNodes = nullptr;
        VkBuffer voronoiNodeBuffer = VK_NULL_HANDLE;
        VkDeviceSize voronoiNodeBufferOffset = 0;
        VkBuffer voronoiNeighborBuffer = VK_NULL_HANDLE;
        VkDeviceSize voronoiNeighborBufferOffset = 0;
        VkBuffer neighborIndicesBuffer = VK_NULL_HANDLE;
        VkDeviceSize neighborIndicesBufferOffset = 0;
        VkBuffer interfaceAreasBuffer = VK_NULL_HANDLE;
        VkDeviceSize interfaceAreasBufferOffset = 0;
        VkBuffer interfaceNeighborIdsBuffer = VK_NULL_HANDLE;
        VkDeviceSize interfaceNeighborIdsBufferOffset = 0;
        VkBuffer seedFlagsBuffer = VK_NULL_HANDLE;
        VkDeviceSize seedFlagsBufferOffset = 0;
        std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeOffsetByModelId;
        std::unordered_map<uint32_t, uint32_t> receiverVoronoiNodeCountByModelId;
        std::unordered_map<uint32_t, VkBuffer> receiverVoronoiSurfaceMappingBufferByModelId;
        std::unordered_map<uint32_t, VkDeviceSize> receiverVoronoiSurfaceMappingBufferOffsetByModelId;
        std::unordered_map<uint32_t, std::vector<uint32_t>> receiverVoronoiSurfaceCellIndicesByModelId;
        std::unordered_map<uint32_t, std::vector<uint32_t>> receiverVoronoiSeedFlagsByModelId;
        std::vector<ContactProduct> contactCouplings;
    };

    HeatSystemController(
        VulkanDevice& vulkanDevice,
        MemoryAllocator& memoryAllocator,
        ModelRegistry& resourceManager,
        UniformBufferManager& uniformBufferManager,
        CommandPool& renderCommandPool,
        uint32_t maxFramesInFlight);

    bool isAnyHeatSystemActive() const;
    bool isAnyHeatSystemPaused() const;
    bool isHeatSystemActive(uint64_t socketKey) const;
    bool isHeatSystemPaused(uint64_t socketKey) const;

    void configure(uint64_t socketKey, const Config& config);
    void disable(uint64_t socketKey);
    void disableAll();
    HeatSystem* getHeatSystem(uint64_t socketKey) const;
    std::vector<HeatSystem*> getActiveSystems() const;

    void createHeatSystem(VkExtent2D extent, VkRenderPass renderPass);
    void updateRenderContext(VkExtent2D extent, VkRenderPass renderPass);
    void updateRenderResources();
    void destroyHeatSystem(uint64_t socketKey);

private:
    struct SystemInstance {
        HeatSystemResources resources;
        std::unique_ptr<HeatSystem> system;
    };

    std::unique_ptr<HeatSystem> buildHeatSystem(HeatSystemResources& heatSystemResources, VkExtent2D extent, VkRenderPass renderPass);
    void configureHeatSystem(HeatSystem& system, const Config& config);

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    ModelRegistry& resourceManager;
    UniformBufferManager& uniformBufferManager;
    CommandPool& renderCommandPool;

    std::unordered_map<uint64_t, SystemInstance> activeSystems;
    std::unordered_map<uint64_t, Config> configuredConfigs;
    const uint32_t maxFramesInFlight;
    VkExtent2D currentExtent = {0, 0};
    VkRenderPass currentRenderPass = VK_NULL_HANDLE;
};

