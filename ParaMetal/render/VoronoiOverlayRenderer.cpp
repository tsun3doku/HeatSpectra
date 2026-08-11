#include "VoronoiOverlayRenderer.hpp"

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

        if (config.runtimeModelId == 0 ||
            config.indexCount == 0 ||
            config.seedPositionBuffer == VK_NULL_HANDLE ||
            config.candidateNeighborIndicesBuffer == VK_NULL_HANDLE ||
            config.candidateBuffer == VK_NULL_HANDLE ||
            config.vertexBuffer == VK_NULL_HANDLE ||
            config.indexBuffer == VK_NULL_HANDLE ||
            config.supportingHalfedgeView == VK_NULL_HANDLE ||
            config.supportingAngleView == VK_NULL_HANDLE ||
            config.halfedgeView == VK_NULL_HANDLE ||
            config.edgeView == VK_NULL_HANDLE ||
            config.triangleView == VK_NULL_HANDLE ||
            config.lengthView == VK_NULL_HANDLE ||
            config.inputHalfedgeView == VK_NULL_HANDLE ||
            config.inputEdgeView == VK_NULL_HANDLE ||
            config.inputTriangleView == VK_NULL_HANDLE ||
            config.inputLengthView == VK_NULL_HANDLE) {
            continue;
        }

        VoronoiRenderer::VoronoiRenderBinding binding{};
        binding.bindingKey = config.bindingKey != 0 ? config.bindingKey : socketKey;
        binding.runtimeModelId = config.runtimeModelId;
        binding.vertexCount = static_cast<uint32_t>(config.intrinsicVertexCount);
        binding.seedBuffer = config.seedPositionBuffer;
        binding.seedOffset = config.seedPositionBufferOffset;
        binding.neighborBuffer = config.candidateNeighborIndicesBuffer;
        binding.neighborOffset = config.candidateNeighborIndicesBufferOffset;
        binding.supportingHalfedgeView = config.supportingHalfedgeView;
        binding.supportingAngleView = config.supportingAngleView;
        binding.halfedgeView = config.halfedgeView;
        binding.edgeView = config.edgeView;
        binding.triangleView = config.triangleView;
        binding.lengthView = config.lengthView;
        binding.inputHalfedgeView = config.inputHalfedgeView;
        binding.inputEdgeView = config.inputEdgeView;
        binding.inputTriangleView = config.inputTriangleView;
        binding.inputLengthView = config.inputLengthView;
        binding.candidateBuffer = config.candidateBuffer;
        binding.candidateOffset = config.candidateBufferOffset;
        binding.vertexBuffer = config.vertexBuffer;
        binding.vertexOffset = config.vertexBufferOffset;
        binding.indexBuffer = config.indexBuffer;
        binding.indexOffset = config.indexBufferOffset;
        binding.indexCount = config.indexCount;
        binding.modelMatrix = config.modelMatrix;
        binding.canonicalToWorldScale = config.canonicalToWorldScale;
        voronoiBindings.push_back(binding);
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
