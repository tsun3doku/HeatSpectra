#include "OverlayPass.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

#include "runtime/RuntimeProducts.hpp"
#include "framegraph/FrameGraphPasses.hpp"
#include "util/File_utils.h"
#include "GeometryPass.hpp"
#include "ContactOverlayRenderer.hpp"
#include "HeatOverlayRenderer.hpp"
#include "VoronoiOverlayRenderer.hpp"
#include "scene/GizmoController.hpp"
#include "renderers/GizmoRenderer.hpp"
#include "spatial/Grid.hpp"
#include "scene/ModelSelection.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "vulkan/ModelRegistry.hpp"
#include "util/Structs.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "vulkan/VulkanDevice.hpp"
#include "renderers/WireframeRenderer.hpp"
#include "renderers/TimingRenderer.hpp"
#include "renderers/IntrinsicRenderer.hpp"
#include "renderers/OutlineRenderer.hpp"
#include "mesh/remesher/iODT.hpp"

namespace render {

OverlayPass::OverlayPass(VulkanDevice& device, MemoryAllocator& allocator, VkFrameGraphRuntime& runtime, ModelRegistry& resources, UniformBufferManager& ubo, GeometryPass& geometry,
    uint32_t framesInFlight, CommandPool& pool, framegraph::PassId passId, framegraph::ResourceId depthResolveId, framegraph::ResourceId depthMsaaId)
    : geometryPass(geometry),
      vulkanDevice(device),
      memoryAllocator(allocator),
      frameGraphRuntime(runtime),
      resourceManager(resources),
      uniformBufferManager(ubo),
      renderCommandPool(pool),
      maxFramesInFlight(framesInFlight),
      passId(passId),
      depthResolveId(depthResolveId),
      depthMsaaId(depthMsaaId) {
}

OverlayPass::~OverlayPass() = default;

const char* OverlayPass::name() const {
    return framegraph::passes::Overlay.data();
}

void OverlayPass::create() {
    ready = false;
    destroy();
    outlineRenderer = std::make_unique<OutlineRenderer>(
        vulkanDevice,
        frameGraphRuntime.getRenderPass(),
        framegraph::toIndex(passId),
        maxFramesInFlight);
    if (!outlineRenderer) {
        std::cerr << "[OverlayPass] Failed to create outline renderer" << std::endl;
        destroy();
        return;
    }

    intrinsicRenderer = std::make_unique<IntrinsicRenderer>(
        vulkanDevice,
        memoryAllocator,
        uniformBufferManager,
        renderCommandPool,
        frameGraphRuntime.getRenderPass(),
        maxFramesInFlight,
        framegraph::toIndex(passId));
    if (!intrinsicRenderer) {
        std::cerr << "[OverlayPass] Failed to create intrinsic renderer" << std::endl;
        destroy();
        return;
    }

    timingOverlay = std::make_unique<TimingRenderer>(vulkanDevice, maxFramesInFlight, frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId), renderCommandPool);
    if (!timingOverlay) {
        std::cerr << "[OverlayPass] Failed to create timing overlay renderer" << std::endl;
        destroy();
        return;
    }

    contactOverlayRenderer = std::make_unique<ContactOverlayRenderer>(
        vulkanDevice,
        memoryAllocator,
        uniformBufferManager);
    if (!contactOverlayRenderer) {
        std::cerr << "[OverlayPass] Failed to create contact overlay renderer" << std::endl;
        destroy();
        return;
    }

    contactOverlayRenderer->initialize(
        frameGraphRuntime.getRenderPass(),
        framegraph::toIndex(passId),
        maxFramesInFlight);

    heatOverlayRenderer = std::make_unique<HeatOverlayRenderer>(
        vulkanDevice,
        memoryAllocator,
        uniformBufferManager);
    if (!heatOverlayRenderer) {
        std::cerr << "[OverlayPass] Failed to create heat overlay renderer" << std::endl;
        destroy();
        return;
    }

    heatOverlayRenderer->initialize(
        frameGraphRuntime.getRenderPass(),
        maxFramesInFlight);

    voronoiOverlayRenderer = std::make_unique<VoronoiOverlayRenderer>(
        vulkanDevice,
        uniformBufferManager,
        renderCommandPool);
    if (!voronoiOverlayRenderer) {
        std::cerr << "[OverlayPass] Failed to create voronoi overlay renderer" << std::endl;
        destroy();
        return;
    }

    voronoiOverlayRenderer->initialize(
        frameGraphRuntime.getRenderPass(),
        framegraph::toIndex(passId),
        maxFramesInFlight);

    gridRenderer = std::make_unique<GridRenderer>(vulkanDevice, uniformBufferManager, maxFramesInFlight, frameGraphRuntime.getRenderPass(), renderCommandPool);
    if (!gridRenderer) {
        std::cerr << "[OverlayPass] Failed to create grid renderer" << std::endl;
        destroy();
        return;
    }
    gizmoRenderer = std::make_unique<GizmoRenderer>(vulkanDevice, frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId), renderCommandPool);
    if (!gizmoRenderer) {
        std::cerr << "[OverlayPass] Failed to create gizmo renderer" << std::endl;
        destroy();
        return;
    }
    ready = true;
}

void OverlayPass::resize(VkExtent2D extent) {
    (void)extent;
}

void OverlayPass::updateDescriptors() {
    if (!ready) {
        return;
    }
    if (outlineRenderer) {
        outlineRenderer->updateDescriptors(
            frameGraphRuntime.getResourceDepthSamplerViews(depthResolveId),
            frameGraphRuntime.getResourceStencilSamplerViews(depthMsaaId));
    }
}

void OverlayPass::setTimingOverlayLines(const std::vector<std::string>& lines) {
    if (timingOverlay) {
        timingOverlay->setLines(lines);
    }
}

void OverlayPass::updateGridLabels(const glm::vec3& gridSize) {
    if (gridRenderer) {
        gridRenderer->updateLabels(gridSize);
    }
}

IntrinsicRenderer* OverlayPass::getIntrinsicRenderer() const {
    return intrinsicRenderer.get();
}

ContactOverlayRenderer* OverlayPass::getContactOverlayRenderer() const {
    return contactOverlayRenderer.get();
}

HeatOverlayRenderer* OverlayPass::getHeatOverlayRenderer() const {
    return heatOverlayRenderer.get();
}

VoronoiOverlayRenderer* OverlayPass::getVoronoiOverlayRenderer() const {
    return voronoiOverlayRenderer.get();
}

void OverlayPass::record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, RenderServices& services) {
    if (!ready) {
        return;
    }
    if (!services.modelSelection || !services.gizmoController || !services.wireframeRenderer) {
        return;
    }

    ModelSelection& modelSelection = *services.modelSelection;
    GizmoController& gizmoController = *services.gizmoController;
    WireframeRenderer& wireframeRenderer = *services.wireframeRenderer;
    VkCommandBuffer commandBuffer = context.commandBuffer;
    const uint32_t currentFrame = context.currentFrame;
    const VkExtent2D extent = context.extent;
    if (intrinsicRenderer) {
        intrinsicRenderer->renderSupportingHalfedges(commandBuffer, currentFrame);
    }

    if (contactOverlayRenderer) {
        contactOverlayRenderer->render(commandBuffer, currentFrame, extent);
    }
    if (heatOverlayRenderer) {
        heatOverlayRenderer->render(commandBuffer, currentFrame);
    }
    if (voronoiOverlayRenderer) {
        voronoiOverlayRenderer->renderSurface(commandBuffer, currentFrame);
        voronoiOverlayRenderer->renderPoints(commandBuffer, currentFrame, extent);
    }

    if (intrinsicRenderer) {
        intrinsicRenderer->renderIntrinsicNormals(commandBuffer, currentFrame);
        intrinsicRenderer->renderIntrinsicVertexNormals(commandBuffer, currentFrame);
    }

    if (outlineRenderer) {
        outlineRenderer->render(commandBuffer, currentFrame, modelSelection);
    }

    if (gridRenderer && currentFrame < gridRenderer->getGridDescriptorSets().size()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gridRenderer->getGridPipeline());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gridRenderer->getGridPipelineLayout(), 0, 1, &gridRenderer->getGridDescriptorSets()[currentFrame], 0, nullptr);
        vkCmdDraw(commandBuffer, gridRenderer->vertexCount, 1, 0, 0);
        gridRenderer->renderLabels(commandBuffer, currentFrame);
    }

    if (flags.wireframeMode > 0) {
        VkDescriptorSet geometryDescriptorSet = geometryPass.getDescriptorSet(currentFrame);
        if (geometryDescriptorSet != VK_NULL_HANDLE) {
            std::vector<WireframeRenderer::DrawItem> wireframeItems;
            for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
                ModelProduct product{};
                if (!resourceManager.exportProduct(modelId, product)) {
                    continue;
                }

                wireframeItems.push_back({ product });
            }

            if (!wireframeItems.empty()) {
                wireframeRenderer.renderModels(
                    commandBuffer,
                    geometryDescriptorSet,
                    wireframeItems.data(),
                    static_cast<uint32_t>(wireframeItems.size()));
            }
        }
    }

    if (timingOverlay) {
        timingOverlay->render(commandBuffer, currentFrame, extent);
    }

    if (modelSelection.getSelected() && gizmoRenderer) {
        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkClearRect clearRect{};
        clearRect.rect.offset = { 0, 0 };
        clearRect.rect.extent = extent;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;

        vkCmdClearAttachments(commandBuffer, 1, &clearAttachment, 1, &clearRect);

        const glm::vec3 gizmoPosition = gizmoController.calculateGizmoPosition(resourceManager, modelSelection);
        const float gizmoScale = gizmoRenderer->calculateGizmoScale(resourceManager, modelSelection);
        gizmoRenderer->render(commandBuffer, gizmoPosition, extent, gizmoScale, view, gizmoController);
    }
}

void OverlayPass::destroy() {
    ready = false;
    if (outlineRenderer) {
        outlineRenderer->cleanup();
        outlineRenderer.reset();
    }
    if (intrinsicRenderer) {
        intrinsicRenderer->cleanup();
        intrinsicRenderer.reset();
    }
    if (contactOverlayRenderer) {
        contactOverlayRenderer->cleanup();
        contactOverlayRenderer.reset();
    }
    if (heatOverlayRenderer) {
        heatOverlayRenderer->cleanup();
        heatOverlayRenderer.reset();
    }
    if (voronoiOverlayRenderer) {
        voronoiOverlayRenderer->cleanup();
        voronoiOverlayRenderer.reset();
    }
    if (timingOverlay) {
        timingOverlay->cleanup();
        timingOverlay.reset();
    }

    if (gridRenderer) {
        gridRenderer->cleanup();
        gridRenderer.reset();
    }
    if (gizmoRenderer) {
        gizmoRenderer->cleanup();
        gizmoRenderer.reset();
    }

}

} // namespace render



