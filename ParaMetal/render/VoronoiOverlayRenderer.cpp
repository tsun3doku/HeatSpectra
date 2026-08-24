#include "VoronoiOverlayRenderer.hpp"

#include <array>
#include <glm/mat4x4.hpp>

#include "renderers/PointRenderer.hpp"
#include "renderers/VoronoiRenderer.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

namespace render {

VoronoiOverlayRenderer::VoronoiOverlayRenderer(VulkanDevice& device, MemoryAllocator& allocator, UniformBufferManager& uniformBufferManager, CommandPool& renderCommandPool)
    : voronoiRenderer(std::make_unique<VoronoiRenderer>(device, allocator, uniformBufferManager, renderCommandPool)),
      pointRenderer(std::make_unique<PointRenderer>(device, uniformBufferManager)) {
}

VoronoiOverlayRenderer::~VoronoiOverlayRenderer() {
    cleanup();
}

void VoronoiOverlayRenderer::initializeSurface(VkRenderPass renderPass, uint32_t surfaceSubpass, uint32_t maxFramesInFlight) {
    if (!voronoiRenderer || surfaceInitialized) {
        return;
    }

    voronoiRenderer->initialize(renderPass, surfaceSubpass, maxFramesInFlight);
    this->maxFramesInFlight = maxFramesInFlight;
    surfaceInitialized = true;
    rebuildBindings();
}

void VoronoiOverlayRenderer::initializeOverlay(VkRenderPass renderPass, uint32_t overlaySubpass, uint32_t maxFramesInFlight) {
    if (!pointRenderer || overlayInitialized) {
        return;
    }

    pointRenderer->initialize(renderPass, overlaySubpass, maxFramesInFlight);
    this->maxFramesInFlight = maxFramesInFlight;
    overlayInitialized = true;
    rebuildBindings();
}

void VoronoiOverlayRenderer::apply(uint64_t socketKey, const VoronoiDisplayController::Config& config) {
    if (socketKey == 0 || !config.anyVisible() || !config.isValid()) {
        remove(socketKey);
        return;
    }

    configsBySocket[socketKey] = config;
    rebuildBindings();
}

void VoronoiOverlayRenderer::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configsBySocket.erase(socketKey);
    rebuildBindings();
}

void VoronoiOverlayRenderer::rebuildBindings() {
    voronoiBindings.clear();
    for (const auto& [socketKey, config] : configsBySocket) {
        if (!config.showVoronoi) {
            continue;
        }

        for (size_t i = 0; i < config.modelRuntimeIds.size(); ++i) {
            if (config.modelRuntimeIds[i] == 0 ||
                config.modelIndexCounts[i] == 0 ||
                config.seedPositionBuffer == VK_NULL_HANDLE ||
                config.candidateNeighborIndicesBuffer == VK_NULL_HANDLE ||
                config.candidateBuffers[i] == VK_NULL_HANDLE ||
                config.modelVertexBuffers[i] == VK_NULL_HANDLE ||
                config.modelIndexBuffers[i] == VK_NULL_HANDLE) {
                continue;
            }

            const std::array<VkBufferView, 10>& views = config.modelBufferViews[i];
            if (views[0] == VK_NULL_HANDLE || views[1] == VK_NULL_HANDLE || views[2] == VK_NULL_HANDLE ||
                views[3] == VK_NULL_HANDLE || views[4] == VK_NULL_HANDLE || views[5] == VK_NULL_HANDLE ||
                views[6] == VK_NULL_HANDLE || views[7] == VK_NULL_HANDLE || views[8] == VK_NULL_HANDLE ||
                views[9] == VK_NULL_HANDLE) {
                continue;
            }

            VoronoiRenderer::RenderBinding binding{};
            binding.bindingKey = socketKey;
            binding.runtimeModelId = config.modelRuntimeIds[i];
            binding.vertexCount = static_cast<uint32_t>(config.intrinsicVertexCounts[i]);
            binding.seedBuffer = config.seedPositionBuffer;
            binding.seedOffset = config.seedPositionBufferOffset;
            binding.neighborBuffer = config.candidateNeighborIndicesBuffer;
            binding.neighborOffset = config.candidateNeighborIndicesBufferOffset;
            binding.supportingHalfedgeView = views[0];
            binding.supportingAngleView = views[1];
            binding.halfedgeView = views[2];
            binding.edgeView = views[3];
            binding.triangleView = views[4];
            binding.lengthView = views[5];
            binding.inputHalfedgeView = views[6];
            binding.inputEdgeView = views[7];
            binding.inputTriangleView = views[8];
            binding.inputLengthView = views[9];
            binding.candidateBuffer = config.candidateBuffers[i];
            binding.candidateOffset = config.candidateBufferOffsets[i];
            binding.vertexBuffer = config.modelVertexBuffers[i];
            binding.vertexOffset = config.modelVertexBufferOffsets[i];
            binding.indexBuffer = config.modelIndexBuffers[i];
            binding.indexOffset = config.modelIndexBufferOffsets[i];
            binding.indexCount = config.modelIndexCounts[i];
            binding.modelMatrix = config.modelMatrices[i];
            binding.canonicalToWorldScale = config.modelCanonicalToWorldScales[i];
            voronoiBindings.push_back(binding);
        }
    }
}

void VoronoiOverlayRenderer::renderSurface(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!surfaceInitialized || !voronoiRenderer) {
        return;
    }

    voronoiRenderer->render(commandBuffer, frameIndex, voronoiBindings);
}

void VoronoiOverlayRenderer::renderPoints(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (!overlayInitialized || !pointRenderer) {
        return;
    }

    for (const auto& [socketKey, config] : configsBySocket) {
        (void)socketKey;
        if (!config.showPoints || config.occupancyPointBuffer == VK_NULL_HANDLE || config.occupancyPointCount == 0) {
            continue;
        }

        pointRenderer->render(
            commandBuffer,
            frameIndex,
            config.occupancyPointBuffer,
            config.occupancyPointBufferOffset,
            config.occupancyPointCount,
            glm::mat4(1.0f),
            extent);
    }
}

void VoronoiOverlayRenderer::cleanup() {
    configsBySocket.clear();
    voronoiBindings.clear();
    maxFramesInFlight = 0;
    surfaceInitialized = false;
    overlayInitialized = false;
    if (voronoiRenderer) {
        voronoiRenderer->cleanup();
    }
    if (pointRenderer) {
        pointRenderer->cleanup();
    }
}

} // namespace render
