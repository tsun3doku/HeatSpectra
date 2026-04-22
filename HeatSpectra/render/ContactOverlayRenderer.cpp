#include "ContactOverlayRenderer.hpp"

#include <vector>

#include <glm/glm.hpp>

#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "vulkan/VulkanDevice.hpp"

namespace render {

ContactLineRenderer::LineVertex toLineVertex(const ContactLineVertex& vertex) {
    ContactLineRenderer::LineVertex lineVertex{};
    lineVertex.position = vertex.position;
    lineVertex.color = vertex.color;
    return lineVertex;
}

ContactOverlayRenderer::ContactOverlayRenderer(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    UniformBufferManager& uniformBufferManager)
    : lineRenderer(std::make_unique<ContactLineRenderer>(device, allocator, uniformBufferManager)) {
}

ContactOverlayRenderer::~ContactOverlayRenderer() {
    cleanup();
}

void ContactOverlayRenderer::initialize(VkRenderPass renderPass, uint32_t subpass, uint32_t maxFramesInFlight) {
    if (!lineRenderer || initialized) {
        return;
    }

    lineRenderer->initialize(renderPass, subpass, maxFramesInFlight);
    initialized = true;
}

void ContactOverlayRenderer::apply(uint64_t socketKey, const ContactDisplayController::Config& config) {
    if (socketKey == 0 || !config.anyVisible() || !config.isValid()) {
        remove(socketKey);
        return;
    }

    configsBySocket[socketKey] = config;
    rebuildLineBuffers();
}

void ContactOverlayRenderer::remove(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    configsBySocket.erase(socketKey);
    rebuildLineBuffers();
}

void ContactOverlayRenderer::render(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkExtent2D extent) {
    if (!initialized || !lineRenderer) {
        return;
    }

    lineRenderer->render(commandBuffer, frameIndex, glm::mat4(1.0f), extent);
}

void ContactOverlayRenderer::rebuildLineBuffers() {
    if (!initialized || !lineRenderer) {
        return;
    }

    std::vector<ContactLineRenderer::LineVertex> outlineVertices;
    std::vector<ContactLineRenderer::LineVertex> correspondenceVertices;

    for (const auto& [socketKey, config] : configsBySocket) {
        (void)socketKey;
        outlineVertices.reserve(outlineVertices.size() + config.outlineVertices.size());
        for (const ContactLineVertex& vertex : config.outlineVertices) {
            outlineVertices.push_back(toLineVertex(vertex));
        }

        correspondenceVertices.reserve(correspondenceVertices.size() + config.correspondenceVertices.size());
        for (const ContactLineVertex& vertex : config.correspondenceVertices) {
            correspondenceVertices.push_back(toLineVertex(vertex));
        }
    }

    lineRenderer->uploadOutlines(outlineVertices);
    lineRenderer->uploadCorrespondences(correspondenceVertices);
}

void ContactOverlayRenderer::cleanup() {
    configsBySocket.clear();
    initialized = false;
    if (lineRenderer) {
        lineRenderer->cleanup();
    }
}

} // namespace render
