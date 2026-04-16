#include "VoronoiOverlayRenderer.hpp"

#include <glm/mat4x4.hpp>

#include "renderers/PointRenderer.hpp"
#include "renderers/VoronoiRenderer.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

namespace render {

VoronoiOverlayRenderer::VoronoiOverlayRenderer(VulkanDevice& device, UniformBufferManager& uniformBufferManager, CommandPool& renderCommandPool)
    : voronoiRenderer(std::make_unique<VoronoiRenderer>(device, uniformBufferManager, renderCommandPool)),
      pointRenderer(std::make_unique<PointRenderer>(device, uniformBufferManager)) {
}

VoronoiOverlayRenderer::~VoronoiOverlayRenderer() {
    cleanup();
}

void VoronoiOverlayRenderer::initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight) {
    if (!voronoiRenderer || !pointRenderer || initialized) {
        return;
    }

    voronoiRenderer->initialize(renderPass, maxFramesInFlight);
    pointRenderer->initialize(renderPass, subpass, maxFramesInFlight);
    initialized = true;
}

void VoronoiOverlayRenderer::apply(uint64_t socketKey, const VoronoiDisplayController::Config& config) {
    if (socketKey == 0 || !config.anyVisible() || !config.isValid()) {
        remove(socketKey);
        return;
    }

    configsBySocket[socketKey] = config;
}

void VoronoiOverlayRenderer::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configsBySocket.erase(socketKey);
}

void VoronoiOverlayRenderer::renderSurface(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!initialized || !voronoiRenderer) {
        return;
    }

    for (const auto& [socketKey, config] : configsBySocket) {
        (void)socketKey;
        if (!config.showVoronoi) {
            continue;
        }

        for (const VoronoiSurfaceProduct& surface : config.surfaces) {
            if (!surface.isValid()) {
                continue;
            }

            voronoiRenderer->updateDescriptors(
                frameIndex,
                surface.intrinsicVertexCount,
                config.seedPositionBuffer,
                config.seedPositionBufferOffset,
                config.neighborIndicesBuffer,
                config.neighborIndicesBufferOffset,
                surface.supportingHalfedgeView,
                surface.supportingAngleView,
                surface.halfedgeView,
                surface.edgeView,
                surface.triangleView,
                surface.lengthView,
                surface.inputHalfedgeView,
                surface.inputEdgeView,
                surface.inputTriangleView,
                surface.inputLengthView,
                surface.candidateBuffer,
                surface.candidateBufferOffset);

            voronoiRenderer->render(
                commandBuffer,
                surface.vertexBuffer,
                surface.vertexBufferOffset,
                surface.indexBuffer,
                surface.indexBufferOffset,
                surface.indexCount,
                frameIndex,
                surface.modelMatrix);
        }
    }
}

void VoronoiOverlayRenderer::renderPoints(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (!initialized || !pointRenderer) {
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
    initialized = false;
    if (voronoiRenderer) {
        voronoiRenderer->cleanup();
    }
    if (pointRenderer) {
        pointRenderer->cleanup();
    }
}

} // namespace render
