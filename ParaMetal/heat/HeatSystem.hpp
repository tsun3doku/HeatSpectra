#pragma once

#include "heat/TimelineSimulation.hpp"
#include "framegraph/ComputePass.hpp"
#include "util/Structs.hpp"
#include "util/Units.hpp"
#include "HeatPlaybackRuntime.hpp"
#include "HeatDomainRuntime.hpp"
#include "HeatSystemPresets.hpp"
#include "voronoi/VoronoiGpuStructs.hpp"

#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <glm/glm.hpp>

class MemoryAllocator;
class VulkanDevice;
class CommandPool;
class HeatSystemSurfaceStage;

class HeatSystem : public ComputePass {
public:
    HeatSystem(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator,
        uint32_t maxFramesInFlight, CommandPool& renderCommandPool, CommandPool& transferCommandPool);
    ~HeatSystem() override;

    void update() override;
    bool ensureConfigured();
    bool setupDescriptors(const std::vector<VkBuffer>& surfaceBuffers, const std::vector<VkDeviceSize>& surfaceOffsets, const std::vector<VkBuffer>& gradientBuffers, const std::vector<VkDeviceSize>& gradientOffsets);
    void recordComputeCommands(VkCommandBuffer commandBuffer, uint32_t currentFrame) override;
    void setComputeTimingQueries(VkQueryPool queryPool, uint32_t startQuery, uint32_t endQuery) override;
    bool createComputeCommandBuffers(uint32_t maxFramesInFlight);
    void cleanup();

    void setActive(bool active);
    void setRewindFrame(uint32_t frame);
    void setPlaybackState(bool paused, uint32_t resetCounter);
    void setSyntheticDirichletTestEnabled(bool enabled) { syntheticDirichletTestEnabled = enabled; }
    void setParams(float contactThermalConductance, float simulationDuration);
    void setHeatModels(
        const std::vector<std::vector<glm::vec3>>& modelSurfacePositions,
        const std::vector<std::vector<glm::vec3>>& modelSurfaceNormals,
        const std::vector<std::vector<uint32_t>>& modelSurfaceTriangleIndices,
        const std::vector<uint32_t>& modelRuntimeModelIds,
        const std::unordered_map<uint32_t, float>& modelInitialTemperaturesCByRuntimeId,
        const std::unordered_map<uint32_t, uint32_t>& modelBoundaryConditionTypesByRuntimeId,
        const std::unordered_map<uint32_t, float>& modelBoundaryTemperaturesCByRuntimeId,
        const std::unordered_map<uint32_t, float>& modelBoundaryHeatFluxesByRuntimeId,
        const std::unordered_map<uint32_t, float>& modelBoundaryHeatTransferCoefficientsByRuntimeId,
        const std::unordered_map<uint32_t, float>& modelVolumetricPowerDensitiesByRuntimeId,
        const std::unordered_map<uint32_t, float>& modelDensity,
        const std::unordered_map<uint32_t, float>& modelSpecificHeat,
        const std::unordered_map<uint32_t, float>& modelConductivity,
        units::LengthUnit worldUnit);

    void clearGlobalVoronoiInput();

    void setGlobalVoronoiInput(
        const HeatDomainRuntime::GlobalThermalDomain& domain,
        const std::unordered_map<uint32_t, std::array<float, 16>>& modelLocalToWorld);

    void resetSimulationState();
    bool setRuntimeDirichletTemperatureC(uint32_t runtimeModelId, uint32_t regionId, float temperatureC);
    bool setRuntimeNeumannHeatFlux(uint32_t runtimeModelId, uint32_t regionId, float heatFlux);
    bool setRuntimeRobinState(uint32_t runtimeModelId, uint32_t regionId, float ambientTemperatureC, float heatTransferCoefficient);
    bool setRuntimeRobinTemperatureC(uint32_t runtimeModelId, uint32_t regionId, float ambientTemperatureC);
    bool setRuntimeVolumetricPowerDensity(uint32_t runtimeModelId, float powerDensity);

    bool getIsActive() const { return isActive; }
    bool getIsPaused() const { return timeline.isPaused(); }
    bool isInitialized() const { return initialized; }
    bool hasDispatchableComputeWork() const override;
    bool voronoiReady() const;

    uint32_t getResetCounter() const { return timeline.getResetCounter(); }
    float getSimulationTimeSeconds() const { return timeline.getCurrentPosition(); }
    uint32_t getRecordedTimelineFrames() const;
    uint32_t getTimelineFrameCount() const;
    float getSimulationDurationSeconds() const { return timeline.getDuration(); }
    uint32_t getRewindFrame() const;

    const std::vector<VkCommandBuffer>& getComputeCommandBuffers() const override { return computeCommandBuffers; }
    ComputePass::Synchronization getSynchronization() const override;
    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& getActiveModels() const { return domainRuntime.getActiveModels(); }
    HeatModelRuntime* getModelByRuntimeId(uint32_t runtimeModelId) const { return domainRuntime.getModelByRuntimeId(runtimeModelId); }

private:
    static constexpr float TimelineFPS = 60.0f;
    static constexpr float FixedTimeStep = 1.0f / 60.0f;

    uint32_t computeTimelineFrameCount() const;
    uint32_t computeHistoryFrameCapacity() const { return computeTimelineFrameCount() + 1; }
    void failInitialization(const char* stage);
    bool rebuildVoronoiRuntime();
    bool rebuildGlobalThermalDomain();
    float getInitialTemperatureC(uint32_t runtimeModelId) const;
    void configureModelProperties(HeatModelRuntime& model, uint32_t runtimeModelId) const;
    bool rebuildDomainRuntime();
    void resetVoronoiTemperatures();

    void processResetTrigger();
    void forwardSim(float deltaTime);
    void configureGMLSSurfaceWeights(bool heatVoronoiReady);
    bool recreateDescriptorPools();
    void configureModelSimResources();
    bool buildModelBoundaryBuffers();

    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& renderCommandPool;
    CommandPool& transferCommandPool;
    std::vector<VkCommandBuffer> computeCommandBuffers;
    VkQueryPool timingQueryPool = VK_NULL_HANDLE;
    uint32_t timingStartQuery = 0;
    uint32_t timingEndQuery = 0;
    uint32_t maxFramesInFlight;

    std::unique_ptr<HeatSystemSurfaceStage> surfaceStage;

    HeatDomainRuntime domainRuntime;
    HeatPlaybackRuntime playbackRuntime;

    std::vector<std::vector<glm::vec3>> modelSurfacePositions;
    std::vector<std::vector<glm::vec3>> modelSurfaceNormals;
    std::vector<std::vector<uint32_t>> modelSurfaceTriangleIndices;
    std::vector<uint32_t> modelRuntimeModelIds;
    std::unordered_map<uint32_t, float> modelInitialTemperaturesCByRuntimeId;
    std::unordered_map<uint32_t, uint32_t> modelBoundaryConditionTypesByRuntimeId;
    std::unordered_map<uint32_t, float> modelBoundaryTemperaturesCByRuntimeId;
    std::unordered_map<uint32_t, float> modelBoundaryHeatFluxesByRuntimeId;
    std::unordered_map<uint32_t, float> modelBoundaryHeatTransferCoefficientsByRuntimeId;
    std::unordered_map<uint32_t, float> modelVolumetricPowerDensitiesByRuntimeId;
    std::unordered_map<uint32_t, float> modelDensity;
    std::unordered_map<uint32_t, float> modelSpecificHeat;
    std::unordered_map<uint32_t, float> modelConductivity;
    units::LengthUnit worldUnit = units::defaultLengthUnit();
    float contactThermalConductance = 16000.0f;

    TimelineSimulation timeline;
    float simulatedTime = 0.0f;
    bool shouldStepPhysics = false;
    bool physicsStepCompletedThisFrame = false;
    bool needsInitialCapture = false;
    bool syntheticDirichletTestEnabled = false;
    float physicsAccumulator = 0.0f;
    bool temperatureBufferAIsCurrent = true;
    std::chrono::steady_clock::time_point lastUpdateTime = std::chrono::steady_clock::now();

    bool isActive = false;
    bool initialized = false;
    bool domainDirty = true;
    bool voronoiConfigDirty = true;
    bool heatParamsDirty = true;
    uint32_t processedResetCounter = 0;

    std::unordered_map<uint32_t, uint32_t> simNodeCounts;
    std::unordered_map<uint32_t, std::array<float, 16>> modelLocalToWorldByModelId;
    std::unordered_map<uint32_t, VkBuffer> modelGMLSSurfaceStencilBufferByModelId;
    std::unordered_map<uint32_t, VkDeviceSize> modelGMLSSurfaceStencilBufferOffsetByModelId;
    std::unordered_map<uint32_t, VkBuffer> modelGMLSSurfaceWeightBufferByModelId;
    std::unordered_map<uint32_t, VkDeviceSize> modelGMLSSurfaceWeightBufferOffsetByModelId;
    std::unordered_map<uint32_t, size_t> modelGMLSSurfaceWeightCountByModelId;
    std::unordered_map<uint32_t, VkBuffer> modelGMLSSurfaceGradientWeightBufferByModelId;
    std::unordered_map<uint32_t, VkDeviceSize> modelGMLSSurfaceGradientWeightBufferOffsetByModelId;
    std::unordered_map<uint32_t, size_t> modelGMLSSurfaceGradientWeightCountByModelId;
};
