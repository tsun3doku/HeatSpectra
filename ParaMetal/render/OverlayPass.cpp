#include "OverlayPass.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

#include "framegraph/FrameGraphPasses.hpp"
#include "util/file_utils.h"
#include "GeometryPass.hpp"
#include "ContactOverlayRenderer.hpp"
#include "HeatOverlayRenderer.hpp"
#include "PointOverlayRenderer.hpp"
#include "VoronoiOverlayRenderer.hpp"
#include "scene/GizmoController.hpp"
#include "renderers/GizmoRenderer.hpp"
#include "renderers/NavigationGizmoRenderer.hpp"
#include "renderers/ScreenTextRenderer.hpp"
#include "scene/NavigationGizmoController.hpp"
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

OverlayPass::OverlayPass(
    VulkanDevice& device,
    MemoryAllocator& allocator,
    VkFrameGraphRuntime& runtime,
    ModelRegistry& resources,
    UniformBufferManager& ubo,
    GeometryPass& geometry,
    HeatOverlayRenderer& heatOverlayRenderer,
    VoronoiOverlayRenderer& voronoiOverlayRenderer,
    IntrinsicRenderer& intrinsicRenderer,
    GizmoRenderer& gizmo,
    ScreenTextRenderer& screenText,
    NavigationGizmoRenderer& navigationGizmo,
    uint32_t framesInFlight,
    CommandPool& pool,
    framegraph::PassId passId,
    framegraph::ResourceId depthResolveId,
    framegraph::ResourceId depthMsaaId)
    : geometryPass(geometry),
      heatOverlayRenderer(heatOverlayRenderer),
      voronoiOverlayRenderer(voronoiOverlayRenderer),
      intrinsicRenderer(intrinsicRenderer),
      gizmoRenderer(gizmo),
      screenTextRenderer(screenText),
      navigationGizmoRenderer(navigationGizmo),
      vulkanDevice(device),
      memoryAllocator(allocator),
      frameGraphRuntime(runtime),
      modelRegistry(resources),
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
    outlineRenderer = std::make_unique<OutlineRenderer>(vulkanDevice);
    if (!outlineRenderer || !outlineRenderer->initialize(frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId), maxFramesInFlight)) {
        std::cerr << "[OverlayPass] Failed to create outline renderer" << std::endl;
        destroy();
        return;
    }

    if (!intrinsicRenderer.initializeOverlay(frameGraphRuntime.getRenderPass(), maxFramesInFlight, framegraph::toIndex(passId))) {
        std::cerr << "[OverlayPass] Failed to create intrinsic renderer" << std::endl;
        destroy();
        return;
    }

    timingOverlay = std::make_unique<TimingRenderer>(screenTextRenderer);
    if (!timingOverlay) {
        std::cerr << "[OverlayPass] Failed to create timing overlay renderer" << std::endl;
        destroy();
        return;
    }

    contactOverlayRenderer = std::make_unique<ContactOverlayRenderer>(vulkanDevice, memoryAllocator, uniformBufferManager, renderCommandPool);
    if (!contactOverlayRenderer) {
        std::cerr << "[OverlayPass] Failed to create contact overlay renderer" << std::endl;
        destroy();
        return;
    }

    contactOverlayRenderer->initialize(frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId), maxFramesInFlight);
    heatOverlayRenderer.initializeOverlay(frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId), maxFramesInFlight);
    voronoiOverlayRenderer.initializeOverlay(frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId), maxFramesInFlight);

    pointOverlayRenderer = std::make_unique<PointOverlayRenderer>(vulkanDevice, uniformBufferManager);
    if (!pointOverlayRenderer) {
        std::cerr << "[OverlayPass] Failed to create point overlay renderer" << std::endl;
        destroy();
        return;
    }

    pointOverlayRenderer->initialize(frameGraphRuntime.getRenderPass(), framegraph::toIndex(passId), maxFramesInFlight);

    gridRenderer = std::make_unique<GridRenderer>(
        vulkanDevice,
        memoryAllocator,
        uniformBufferManager,
        maxFramesInFlight,
        frameGraphRuntime.getRenderPass(),
        framegraph::toIndex(passId),
        renderCommandPool);
    if (!gridRenderer) {
        std::cerr << "[OverlayPass] Failed to create grid renderer" << std::endl;
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

void OverlayPass::updateGrid(
    uint32_t frameIndex,
    const SceneView& sceneView,
    const glm::vec3& sceneExtent) {
    if (gridRenderer) gridRenderer->update(frameIndex, sceneView, sceneExtent);
}

void OverlayPass::setWorldUnit(units::LengthUnit unit) {
    if (gridRenderer) gridRenderer->setWorldUnit(unit);
}

IntrinsicRenderer* OverlayPass::getIntrinsicRenderer() const {
    return &intrinsicRenderer;
}

ContactOverlayRenderer* OverlayPass::getContactOverlayRenderer() const {
    return contactOverlayRenderer.get();
}

HeatOverlayRenderer* OverlayPass::getHeatOverlayRenderer() const {
    return &heatOverlayRenderer;
}

VoronoiOverlayRenderer* OverlayPass::getVoronoiOverlayRenderer() const {
    return &voronoiOverlayRenderer;
}

PointOverlayRenderer* OverlayPass::getPointOverlayRenderer() const {
    return pointOverlayRenderer.get();
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
    screenTextRenderer.beginFrame(currentFrame);
    const VkExtent2D extent = context.extent;
    if (contactOverlayRenderer) {
        contactOverlayRenderer->render(commandBuffer, currentFrame, extent);
    }
    heatOverlayRenderer.renderOverlay(commandBuffer, currentFrame);
    voronoiOverlayRenderer.renderPoints(commandBuffer, currentFrame, extent);
    if (pointOverlayRenderer) {
        pointOverlayRenderer->render(commandBuffer, currentFrame, extent);
    }

    intrinsicRenderer.renderOverlay(commandBuffer, currentFrame);

    if (outlineRenderer) {
        outlineRenderer->render(commandBuffer, currentFrame, modelSelection);
    }

    if (flags.drawGrid && gridRenderer && currentFrame < gridRenderer->getGridDescriptorSets().size()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gridRenderer->getGridPipeline());
        vkCmdBindDescriptorSets(
            commandBuffer, 
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            gridRenderer->getGridPipelineLayout(), 
            0, 
            1, 
            &gridRenderer->getGridDescriptorSets()[currentFrame], 
            0, 
            nullptr);
        vkCmdDraw(commandBuffer, gridRenderer->vertexCount, 1, 0, 0);
        gridRenderer->renderLabels(commandBuffer, currentFrame);
    }

    if (flags.wireframeMode > 0) {
        VkDescriptorSet geometryDescriptorSet = geometryPass.getDescriptorSet(currentFrame);
        if (geometryDescriptorSet != VK_NULL_HANDLE) {
            wireframeRenderer.bindPipeline(commandBuffer);
            for (uint32_t modelId : modelRegistry.getRenderableModelIds()) {
                VkBuffer vertexBuffer = VK_NULL_HANDLE;
                VkDeviceSize vertexBufferOffset = 0;
                VkBuffer indexBuffer = VK_NULL_HANDLE;
                VkDeviceSize indexBufferOffset = 0;
                uint32_t indexCount = 0;
                glm::mat4 modelMatrix{1.0f};
                if (!modelRegistry.tryGetRenderGeometry(
                        modelId, vertexBuffer, vertexBufferOffset,
                        indexBuffer, indexBufferOffset, indexCount) ||
                    !modelRegistry.tryGetModelMatrix(modelId, modelMatrix)) {
                    continue;
                }
                wireframeRenderer.renderModel(
                    commandBuffer, geometryDescriptorSet, modelMatrix,
                    vertexBuffer, vertexBufferOffset,
                    indexBuffer, indexBufferOffset, indexCount);
            }
        }
    }

    if (timingOverlay) {
        timingOverlay->render(commandBuffer, currentFrame, extent);
    }

    if (modelSelection.getSelected()) {
        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

        VkClearRect clearRect{};
        clearRect.rect.offset = { 0, 0 };
        clearRect.rect.extent = extent;
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;

        vkCmdClearAttachments(commandBuffer, 1, &clearAttachment, 1, &clearRect);

        const glm::vec3 gizmoPosition = gizmoController.calculateGizmoPosition(modelRegistry, modelSelection);
        const float gizmoScale = gizmoRenderer.calculateGizmoScale(modelRegistry, modelSelection);
        gizmoRenderer.render(commandBuffer, gizmoPosition, extent, gizmoScale, view, gizmoController);
    }

    if (services.navigationGizmoController) {
        navigationGizmoRenderer.render(commandBuffer, services.navigationGizmoController->getRenderData());
    }
}

void OverlayPass::destroy() {
    ready = false;
    if (outlineRenderer) {
        outlineRenderer->cleanup();
        outlineRenderer.reset();
    }
    if (contactOverlayRenderer) {
        contactOverlayRenderer->cleanup();
        contactOverlayRenderer.reset();
    }
    if (pointOverlayRenderer) {
        pointOverlayRenderer->cleanup();
        pointOverlayRenderer.reset();
    }
    if (timingOverlay) {
        timingOverlay->cleanup();
        timingOverlay.reset();
    }
    if (gridRenderer) {
        gridRenderer->cleanup();
        gridRenderer.reset();
    }
}

} // namespace render



