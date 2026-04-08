#include "HeatSystemRenderStage.hpp"

#include "HeatReceiverRuntime.hpp"
#include "HeatSourceRuntime.hpp"
#include "runtime/HeatOverlayData.hpp"
#include "renderers/HeatReceiverRenderer.hpp"
#include "renderers/HeatSourceRenderer.hpp"
#include "scene/Model.hpp"
#include "vulkan/ModelRegistry.hpp"

HeatSystemRenderStage::HeatSystemRenderStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
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
            if (runtimeSourceBinding.runtimeModelId == 0 || !runtimeSourceBinding.heatSource) {
                continue;
            }

            HeatOverlayData sourceOverlayBinding{};
            sourceOverlayBinding.runtimeModelId = runtimeSourceBinding.runtimeModelId;
            sourceOverlayBinding.sourceTemperature = runtimeSourceBinding.heatSource->getUniformTemperature();
            sourceOverlayBinding.sourceBufferView = runtimeSourceBinding.heatSource->getSourceBufferView();
            sourceOverlayBinding.sourceVertexCount = static_cast<uint32_t>(runtimeSourceBinding.heatSource->getVertexCount());
            sourceRenderBindings.push_back(sourceOverlayBinding);
        }

        heatSourceRenderer->render(cmdBuffer, frameIndex, sourceRenderBindings, context.resourceManager);
    }

    if (heatReceiverRenderer) {
        std::vector<HeatReceiverRenderer::ReceiverRenderBinding> receiverRenderBindings;
        receiverRenderBindings.reserve(receivers.size());
        for (const auto& receiver : receivers) {
            if (!receiver || receiver->getRuntimeModelId() == 0) {
                continue;
            }

            HeatReceiverRenderer::ReceiverRenderBinding receiverBinding{};
            receiverBinding.runtimeModelId = receiver->getRuntimeModelId();
            receiverRenderBindings.push_back(receiverBinding);
        }

        heatReceiverRenderer->render(cmdBuffer, frameIndex, receiverRenderBindings, context.resourceManager);
    }
}

