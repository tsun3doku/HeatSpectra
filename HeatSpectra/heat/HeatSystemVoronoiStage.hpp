#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "HeatSystemPresets.hpp"
#include "HeatSystemStageContext.hpp"
#include "spatial/VoxelGrid.hpp"
#include "util/Structs.hpp"
#include "voronoi/VoronoiIntegrator.hpp"
#include "voronoi/VoronoiSeeder.hpp"

class HeatReceiver;
class PointRenderer;
class VoronoiGeoCompute;

struct HeatSystemVoronoiDomain {
    uint32_t receiverModelId = 0;
    HeatReceiver* receiver = nullptr;
    std::unique_ptr<VoronoiSeeder> seeder;
    std::unique_ptr<VoronoiIntegrator> integrator;
    std::vector<uint32_t> seedFlags;
    VoxelGrid voxelGrid;
    bool voxelGridBuilt = false;
    uint32_t nodeOffset = 0;
    uint32_t nodeCount = 0;
};

class HeatSystemVoronoiStage {
public:
    explicit HeatSystemVoronoiStage(const HeatSystemStageContext& stageContext);

    void onResourcesRecreated(uint32_t maxFramesInFlight, VkExtent2D extent, VkRenderPass renderPass);
    bool buildReceiverDomains(
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
        std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains,
        uint32_t maxNeighbors) const;
    bool generateVoronoiDiagram(
        std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains,
        const std::unordered_map<uint32_t, HeatMaterialPresetId>& receiverMaterialPresetByModelId,
        bool debugEnable,
        uint32_t maxFramesInFlight,
        uint32_t maxNeighbors,
        VoronoiGeoCompute* voronoiGeoCompute,
        PointRenderer* pointRenderer);
    void dispatchDiffusionSubstep(
        VkCommandBuffer commandBuffer,
        uint32_t currentFrame,
        const HeatSourcePushConstant& basePushConstant,
        int substepIndex,
        uint32_t workGroupCount) const;
    void insertInterSubstepBarrier(VkCommandBuffer commandBuffer, int substepIndex, uint32_t numSubsteps) const;
    void insertFinalTemperatureBarrier(VkCommandBuffer commandBuffer, uint32_t numSubsteps) const;
    bool finalSubstepWritesBufferB(uint32_t numSubsteps) const;
    bool createDescriptorPool(uint32_t maxFramesInFlight);
    bool createDescriptorSetLayout();
    bool createDescriptorSets(uint32_t maxFramesInFlight);
    bool createPipeline();

private:
    static constexpr float AMBIENT_TEMPERATURE = 1.0f;

    bool tryCreateStorageBuffer(
        const char* label,
        const void* data,
        VkDeviceSize size,
        VkBuffer& buffer,
        VkDeviceSize& offset,
        void** mapped,
        bool hostVisible = true) const;
    bool createVoronoiGeometryBuffers(
        const std::vector<VoronoiNodeGPU>& initialNodes,
        const std::vector<glm::vec4>& seedPositions,
        const std::vector<uint32_t>& seedFlags,
        const std::vector<uint32_t>& neighborIndices,
        bool debugEnable,
        uint32_t maxNeighbors);
    bool buildVoronoiNeighborBuffer(uint32_t maxNeighbors);
    void initializeVoronoi();
    void uploadOccupancyPoints(const std::vector<HeatSystemVoronoiDomain>& domains, PointRenderer* pointRenderer) const;

    HeatSystemStageContext context;
};
