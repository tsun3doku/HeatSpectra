#include "HeatOverlayRenderer.hpp"

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "domain/HeatModelData.hpp"

#include <iostream>
#include <algorithm>
#include <cmath>

namespace render {

HeatOverlayRenderer::HeatOverlayRenderer(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    UniformBufferManager& uniformBufferManager,
    CommandPool& commandPool)
    : vulkanDevice(device),
      commandPool(commandPool),
      memoryAllocator(allocator),
      surfaceRenderer(std::make_unique<HeatSurfaceRenderer>(device, allocator, uniformBufferManager, commandPool)),
      vectorArrowRenderer(std::make_unique<VectorArrowRenderer>(device, allocator, uniformBufferManager, commandPool)) {
}

HeatOverlayRenderer::~HeatOverlayRenderer() {
    cleanup();
}

void HeatOverlayRenderer::initializeSurface(VkRenderPass renderPass, uint32_t surfaceSubpass, uint32_t updatedMaxFramesInFlight) {
    if (!surfaceRenderer || surfaceInitialized) {
        return;
    }

    maxFramesInFlight = updatedMaxFramesInFlight;
    surfaceRenderer->initialize(renderPass, surfaceSubpass, maxFramesInFlight);
    surfaceInitialized = true;
    rebuildBindings();
}

void HeatOverlayRenderer::initializeOverlay(VkRenderPass renderPass, uint32_t overlaySubpass, uint32_t updatedMaxFramesInFlight) {
    if (!vectorArrowRenderer || overlayInitialized) {
        return;
    }

    maxFramesInFlight = updatedMaxFramesInFlight;
    vectorArrowRenderer->initialize(renderPass, overlaySubpass, maxFramesInFlight);
    overlayInitialized = true;
    rebuildBindings();
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
    if (!surfaceRenderer || !vectorArrowRenderer) {
        return;
    }

    surfaceBindings.clear();
    fluxVectorBindings.clear();

    paletteVisible = false;

    for (const auto& [socketKey, config] : configsBySocket) {
        if (!config.active) continue;

        if (config.showHeatPalette) {
            paletteVisible = true;
        }

        if (config.showHeatOverlay) {
            surfaceBindings.reserve(surfaceBindings.size() + config.modelRuntimeIds.size());
            for (size_t index = 0; index < config.modelRuntimeIds.size(); ++index) {
                if (config.modelRuntimeIds[index] == 0 ||
                    config.modelRenderVertexBuffers[index] == VK_NULL_HANDLE ||
                    config.modelRenderIndexBuffers[index] == VK_NULL_HANDLE ||
                    config.modelRenderIndexCounts[index] == 0) {
                    continue;
                }

                if (index >= config.modelSurfaceBuffers.size() ||
                    config.modelSurfaceBuffers[index] == VK_NULL_HANDLE ||
                    index >= config.modelBufferViews.size()) {
                    continue;
                }

                bool viewsValid = true;
                for (int j = 0; j < 10; ++j) {
                    if (config.modelBufferViews[index][j] == VK_NULL_HANDLE) {
                        viewsValid = false;
                        break;
                    }
                }
                if (!viewsValid) {
                    continue;
                }

                HeatSurfaceRenderer::SurfaceRenderBinding binding{};
                binding.runtimeModelId = config.modelRuntimeIds[index];
                binding.vertexBuffer = config.modelRenderVertexBuffers[index];
                binding.vertexBufferOffset = config.modelRenderVertexBufferOffsets[index];
                binding.indexBuffer = config.modelRenderIndexBuffers[index];
                binding.indexBufferOffset = config.modelRenderIndexBufferOffsets[index];
                binding.indexCount = config.modelRenderIndexCounts[index];
                binding.modelMatrix = config.modelMatrices[index];
                binding.bufferViews = config.modelBufferViews[index];
                binding.surfaceBuffer = config.modelSurfaceBuffers[index];
                binding.surfaceBufferOffset = config.modelSurfaceBufferOffsets[index];

                surfaceBindings.push_back(binding);
            }
        }

        if (config.showFluxVectors) {
            for (size_t index = 0; index < config.modelRuntimeIds.size(); ++index) {
                if (config.modelRuntimeIds[index] != 0 &&
                    index < config.modelSurfaceBuffers.size() &&
                    config.modelSurfaceBuffers[index] != VK_NULL_HANDLE &&
                    index < config.modelSurfaceBufferOffsets.size() &&
                    index < config.modelSurfaceGradientBuffers.size() &&
                    index < config.modelSurfaceGradientBufferOffsets.size() &&
                    index < config.modelSurfacePointCounts.size() &&
                    config.modelSurfaceGradientBuffers[index] != VK_NULL_HANDLE &&
                    config.modelSurfacePointCounts[index] != 0) {
                    VectorArrowRenderer::VectorRenderBinding vectorBinding{};
                    vectorBinding.bindingKey = config.modelRuntimeIds[index];
                    vectorBinding.surfaceBuffer = config.modelSurfaceBuffers[index];
                    vectorBinding.surfaceBufferOffset = config.modelSurfaceBufferOffsets[index];
                    vectorBinding.gradientBuffer = config.modelSurfaceGradientBuffers[index];
                    vectorBinding.gradientBufferOffset = config.modelSurfaceGradientBufferOffsets[index];
                    vectorBinding.sampleCount = config.modelSurfacePointCounts[index];
                    vectorBinding.modelMatrix = config.modelMatrices[index];
                    vectorBinding.scale = config.fluxVectorScale;
                    vectorBinding.canonicalToWorldScale = config.canonicalToWorldScale;

                    fluxVectorBindings.push_back(vectorBinding);
                }
            }
        }
    }
}

void HeatOverlayRenderer::renderSurface(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!surfaceInitialized || !surfaceRenderer) {
        return;
    }

    surfaceRenderer->render(commandBuffer, frameIndex, surfaceBindings);
}

void HeatOverlayRenderer::setPaletteRange(float minimum, float maximum) {
    if (std::isfinite(minimum) && std::isfinite(maximum) && maximum > minimum) {
        if (surfaceRenderer) surfaceRenderer->setRange(minimum, maximum);
    }
}
void HeatOverlayRenderer::setPalette(int palette) {
    if (surfaceRenderer) surfaceRenderer->setPalette(palette);
}

void HeatOverlayRenderer::renderOverlay(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!overlayInitialized || !vectorArrowRenderer) {
        return;
    }

    vectorArrowRenderer->render(commandBuffer, frameIndex, fluxVectorBindings);
}

void HeatOverlayRenderer::cleanup() {
    configsBySocket.clear();
    surfaceBindings.clear();
    fluxVectorBindings.clear();
    maxFramesInFlight = 0;
    surfaceInitialized = false;
    overlayInitialized = false;
    if (surfaceRenderer) {
        surfaceRenderer->cleanup();
    }
    if (vectorArrowRenderer) {
        vectorArrowRenderer->cleanup();
    }
}

} // namespace render
