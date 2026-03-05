#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include "HeatSystemRuntime.hpp"
#include "HeatSystemStageContext.hpp"
#include "HeatSystemVoronoiStage.hpp"

class ContactLineRenderer;
class HeatReceiver;
class HeatRenderer;
class PointRenderer;
class VoronoiRenderer;

class HeatSystemRenderStage {
public:
    explicit HeatSystemRenderStage(const HeatSystemStageContext& stageContext);

    void renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent, ContactLineRenderer* contactLineRenderer, bool isActive) const;
    void renderVoronoiSurface(
        VkCommandBuffer cmdBuffer,
        uint32_t frameIndex,
        VoronoiRenderer* voronoiRenderer,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
        const std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains,
        bool isActive) const;
    void renderHeatOverlay(
        VkCommandBuffer cmdBuffer,
        uint32_t frameIndex,
        HeatRenderer* heatRenderer,
        const std::vector<HeatSystemRuntime::SourceCoupling>& sourceCouplings,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
        bool isActive,
        bool isPaused) const;
    void renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent, PointRenderer* pointRenderer, bool isActive) const;
    void renderSurfels(
        VkCommandBuffer cmdBuffer,
        uint32_t frameIndex,
        float radius,
        const std::vector<HeatSystemRuntime::SourceCoupling>& sourceCouplings,
        const std::vector<std::unique_ptr<HeatReceiver>>& receivers) const;

private:
    const HeatSystemVoronoiDomain* findReceiverDomainByModelId(const std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains, uint32_t receiverModelId) const;
    const HeatSystemRuntime::SourceCoupling* findSourceCouplingForModel(
        const std::vector<HeatSystemRuntime::SourceCoupling>& sourceCouplings,
        const class Model* model) const;

    HeatSystemStageContext context;
};
