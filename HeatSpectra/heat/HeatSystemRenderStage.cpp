#include "HeatSystemRenderStage.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSourceRuntime.hpp"
#include "runtime/HeatOverlayData.hpp"
#include "renderers/ContactLineRenderer.hpp"
#include "renderers/HeatReceiverRenderer.hpp"
#include "renderers/HeatSourceRenderer.hpp"
#include "scene/Model.hpp"
#include "vulkan/ResourceManager.hpp"

#include <glm/glm.hpp>

HeatSystemRenderStage::HeatSystemRenderStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

void HeatSystemRenderStage::renderContactLines(
    VkCommandBuffer cmdBuffer,
    uint32_t frameIndex,
    VkExtent2D extent,
    ContactLineRenderer* contactLineRenderer) const {
    if (!contactLineRenderer) {
        return;
    }

    contactLineRenderer->render(cmdBuffer, frameIndex, glm::mat4(1.0f), extent);
}

void HeatSystemRenderStage::renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex,
    HeatSourceRenderer* heatSourceRenderer, HeatReceiverRenderer* heatReceiverRenderer,
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings, const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers,
    bool isActive, bool isPaused) const {
    if ((!heatSourceRenderer && !heatReceiverRenderer) || (!isActive && !isPaused)) {
        return;
    }

    if (heatSourceRenderer) {
        std::vector<HeatOverlayData> sourceRenderBindings;
        sourceRenderBindings.reserve(sourceBindings.size());
        for (const HeatSystemRuntime::SourceBinding& runtimeSourceBinding : sourceBindings) {
            if (runtimeSourceBinding.geometryPackage.runtimeModelId == 0 || !runtimeSourceBinding.heatSource) {
                continue;
            }

            Model* sourceModel = context.resourceManager.getModelByID(runtimeSourceBinding.geometryPackage.runtimeModelId);
            if (!sourceModel) {
                continue;
            }

            HeatOverlayData sourceOverlayBinding{};
            sourceOverlayBinding.model = sourceModel;
            sourceOverlayBinding.sourceTemperature = runtimeSourceBinding.heatSource->getUniformTemperature();
            sourceOverlayBinding.sourceBufferView = runtimeSourceBinding.heatSource->getSourceBufferView();
            sourceOverlayBinding.sourceVertexCount = static_cast<uint32_t>(runtimeSourceBinding.heatSource->getVertexCount());
            sourceRenderBindings.push_back(sourceOverlayBinding);
        }

        heatSourceRenderer->render(cmdBuffer, frameIndex, sourceRenderBindings);
    }

    if (heatReceiverRenderer) {
        std::vector<HeatReceiverRenderer::ReceiverRenderBinding> receiverRenderBindings;
        receiverRenderBindings.reserve(receivers.size());
        for (const auto& receiver : receivers) {
            if (!receiver) {
                continue;
            }

            Model* receiverModel = context.resourceManager.getModelByID(receiver->getRuntimeModelId());
            if (!receiverModel) {
                continue;
            }

            HeatReceiverRenderer::ReceiverRenderBinding receiverBinding{};
            receiverBinding.model = receiverModel;
            receiverRenderBindings.push_back(receiverBinding);
        }

        heatReceiverRenderer->render(cmdBuffer, frameIndex, receiverRenderBindings);
    }
}

void HeatSystemRenderStage::renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, float radius,
    const std::vector<HeatSystemRuntime::SourceBinding>& sourceBindings, const std::vector<std::unique_ptr<HeatReceiverRuntime>>& receivers) const {
    (void)cmdBuffer;
    (void)frameIndex;
    (void)radius;
    (void)sourceBindings;
    (void)receivers;
}
