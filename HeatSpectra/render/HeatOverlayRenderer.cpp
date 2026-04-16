#include "HeatOverlayRenderer.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

namespace render {

HeatOverlayRenderer::HeatOverlayRenderer(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    UniformBufferManager& uniformBufferManager)
    : memoryAllocator(allocator),
      sourceRenderer(std::make_unique<HeatSourceRenderer>(device, uniformBufferManager)),
      receiverRenderer(std::make_unique<HeatReceiverRenderer>(device, uniformBufferManager)) {
}

HeatOverlayRenderer::~HeatOverlayRenderer() {
    cleanup();
}

void HeatOverlayRenderer::initialize(VkRenderPass renderPass, uint32_t updatedMaxFramesInFlight) {
    if (!sourceRenderer || !receiverRenderer || initialized) {
        return;
    }

    maxFramesInFlight = updatedMaxFramesInFlight;
    sourceRenderer->initialize(renderPass);
    receiverRenderer->initialize(renderPass, maxFramesInFlight);
    initialized = true;
}

void HeatOverlayRenderer::apply(uint64_t socketKey, const HeatDisplayController::Config& config) {
    if (socketKey == 0 || !config.anyVisible() || !config.isValid()) {
        remove(socketKey);
        return;
    }

    configsBySocket[socketKey] = config;
    rebuildBindings();
}

void HeatOverlayRenderer::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configsBySocket.erase(socketKey);
    rebuildBindings();
}

void HeatOverlayRenderer::rebuildBindings() {
    if (!initialized || !sourceRenderer || !receiverRenderer) {
        return;
    }

    sourceBindings.clear();
    receiverBindings.clear();

    for (const auto& [socketKey, config] : configsBySocket) {
        (void)socketKey;
        if (!config.active && !config.paused) {
            continue;
        }

        sourceBindings.reserve(sourceBindings.size() + config.sourceModels.size());
        for (size_t index = 0; index < config.sourceModels.size(); ++index) {
            const ModelProduct& sourceModel = config.sourceModels[index];
            if (!sourceModel.isValid()) {
                continue;
            }

            HeatSourceRenderer::SourceRenderBinding binding{};
            binding.model = sourceModel;
            binding.sourceTemperature = config.sourceTemperatures[index];
            sourceBindings.push_back(binding);
        }

        receiverBindings.reserve(receiverBindings.size() + config.receiverModels.size());
        for (size_t index = 0; index < config.receiverModels.size(); ++index) {
            if (!config.receiverModels[index].isValid()) {
                continue;
            }

            HeatReceiverRenderer::ReceiverRenderBinding binding{};
            binding.model = config.receiverModels[index];
            binding.bufferViews = config.receiverBufferViews[index];
            receiverBindings.push_back(binding);
        }
    }

    receiverRenderer->updateDescriptors(receiverBindings, maxFramesInFlight, false);
}

void HeatOverlayRenderer::render(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!initialized || !sourceRenderer || !receiverRenderer) {
        return;
    }

    sourceRenderer->render(commandBuffer, frameIndex, sourceBindings);
    receiverRenderer->render(commandBuffer, frameIndex, receiverBindings);
}

void HeatOverlayRenderer::cleanup() {
    configsBySocket.clear();
    sourceBindings.clear();
    receiverBindings.clear();
    maxFramesInFlight = 0;
    initialized = false;
    if (sourceRenderer) {
        sourceRenderer->cleanup();
    }
    if (receiverRenderer) {
        receiverRenderer->cleanup();
    }
}

} // namespace render
