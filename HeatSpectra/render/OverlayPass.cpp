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
#include "contact/ContactSystem.hpp"
#include "heat/HeatSystem.hpp"
#include "heat/VoronoiSystem.hpp"

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

void OverlayPass::bindRemeshProduct(uint64_t socketKey, const RemeshProduct& product) {
    if (intrinsicRenderer) {
        intrinsicRenderer->bindRemeshProduct(socketKey, product);
    }
}

void OverlayPass::removeIntrinsicPackage(uint64_t packageKey) {
    if (intrinsicRenderer) {
        intrinsicRenderer->removeIntrinsicPackage(packageKey);
    }
}

void OverlayPass::record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) {
    if (!ready) {
        return;
    }
    if (!services.modelSelection || !services.gizmoController || !services.wireframeRenderer) {
        return;
    }

    ModelSelection& modelSelection = *services.modelSelection;
    GizmoController& gizmoController = *services.gizmoController;
    WireframeRenderer& wireframeRenderer = *services.wireframeRenderer;
    const std::vector<HeatSystem*>& heatSystems = services.heatSystems;
    const std::vector<VoronoiSystem*>& voronoiSystems = services.voronoiSystems;
    const std::vector<ContactSystem*>& contactSystems = services.contactSystems;

    VkCommandBuffer commandBuffer = context.commandBuffer;
    const uint32_t currentFrame = context.currentFrame;
    const VkExtent2D extent = context.extent;

    if (flags.drawHeatOverlay) {
        for (HeatSystem* heatSystem : heatSystems) {
            if (heatSystem && (heatSystem->getIsActive() || heatSystem->getIsPaused())) {

                heatSystem->renderHeatOverlay(commandBuffer, currentFrame);
            }
        }
    }

    if (flags.drawIntrinsicOverlay && intrinsicRenderer) {
        intrinsicRenderer->renderSupportingHalfedges(commandBuffer, currentFrame, resourceManager);
    }

    if (params.drawIntrinsicNormals && intrinsicRenderer) {
        intrinsicRenderer->renderIntrinsicNormals(commandBuffer, currentFrame, resourceManager, params.normalLength);
    }

    if (params.drawIntrinsicVertexNormals && intrinsicRenderer) {
        intrinsicRenderer->renderIntrinsicVertexNormals(commandBuffer, currentFrame, resourceManager, params.normalLength);
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

    if (flags.drawVoronoi) {
        for (VoronoiSystem* voronoiSystem : voronoiSystems) {
            if (voronoiSystem && voronoiSystem->isReady()) {

                voronoiSystem->renderVoronoiSurface(commandBuffer, currentFrame);
            }
        }
    }
    if (flags.drawPoints) {
        for (VoronoiSystem* voronoiSystem : voronoiSystems) {
            if (voronoiSystem && voronoiSystem->isReady()) {
                voronoiSystem->renderOccupancy(commandBuffer, currentFrame, extent);
            }
        }
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

    if (flags.drawContactLines) {
        for (ContactSystem* contactSystem : contactSystems) {
            if (contactSystem) {
                contactSystem->renderContactLines(commandBuffer, currentFrame, extent);
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



