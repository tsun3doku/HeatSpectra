#include "OverlayPass.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

#include "framegraph/FrameGraphPasses.hpp"
#include "util/File_utils.h"
#include "GeometryPass.hpp"
#include "scene/GizmoController.hpp"
#include "renderers/GizmoRenderer.hpp"
#include "spatial/Grid.hpp"
#include "scene/Model.hpp"
#include "scene/ModelSelection.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "vulkan/ResourceManager.hpp"
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

OverlayPass::OverlayPass(VulkanDevice& device, VkFrameGraphRuntime& runtime, ResourceManager& resources, UniformBufferManager& ubo, GeometryPass& geometry, 
    uint32_t framesInFlight, CommandPool& pool, framegraph::PassId passId, framegraph::ResourceId depthResolveId, framegraph::ResourceId depthMsaaId)
    : geometryPass(geometry),
      vulkanDevice(device),
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

void OverlayPass::allocateDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (intrinsicRenderer) {
        intrinsicRenderer->allocateDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void OverlayPass::updateDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight) {
    if (intrinsicRenderer) {
        intrinsicRenderer->updateDescriptorSetsForModel(model, remesher, maxFramesInFlight);
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

void OverlayPass::allocateNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (intrinsicRenderer) {
        intrinsicRenderer->allocateNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void OverlayPass::updateNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight) {
    if (intrinsicRenderer) {
        intrinsicRenderer->updateNormalsDescriptorSetsForModel(model, remesher, maxFramesInFlight);
    }
}

void OverlayPass::allocateVertexNormalsDescriptorSetsForModel(Model* model, uint32_t maxFramesInFlight) {
    if (intrinsicRenderer) {
        intrinsicRenderer->allocateVertexNormalsDescriptorSetsForModel(model, maxFramesInFlight);
    }
}

void OverlayPass::updateVertexNormalsDescriptorSetsForModel(Model* model, iODT* remesher, uint32_t maxFramesInFlight) {
    if (intrinsicRenderer) {
        intrinsicRenderer->updateVertexNormalsDescriptorSetsForModel(model, remesher, maxFramesInFlight);
    }
}

void OverlayPass::record(const FrameContext& context, const SceneView& view, const RenderFlags& flags, const OverlayParams& params, RenderServices& services) {
    if (!ready) {
        return;
    }
    if (!services.remesher || !services.modelSelection || !services.gizmoController || !services.wireframeRenderer) {
        return;
    }

    Remesher& remesher = *services.remesher;
    ModelSelection& modelSelection = *services.modelSelection;
    GizmoController& gizmoController = *services.gizmoController;
    WireframeRenderer& wireframeRenderer = *services.wireframeRenderer;
    FrameSimulation* simulation = services.simulation;

    VkCommandBuffer commandBuffer = context.commandBuffer;
    const uint32_t currentFrame = context.currentFrame;
    const VkExtent2D extent = context.extent;

    if (simulation && flags.drawHeatOverlay && (simulation->simulationActive() || simulation->simulationPaused())) {
        simulation->renderHeatOverlay(commandBuffer, currentFrame);
    }

    if (flags.drawIntrinsicOverlay && intrinsicRenderer) {
        intrinsicRenderer->renderSupportingHalfedges(commandBuffer, currentFrame, remesher);
    }

    if (params.drawIntrinsicNormals && intrinsicRenderer) {
        intrinsicRenderer->renderIntrinsicNormals(commandBuffer, currentFrame, remesher, params.normalLength);
    }

    if (params.drawIntrinsicVertexNormals && intrinsicRenderer) {
        intrinsicRenderer->renderIntrinsicVertexNormals(commandBuffer, currentFrame, remesher, params.normalLength);
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

    if (simulation && flags.drawSurfels && simulation->simulationActive() && simulation->voronoiReady()) {
        simulation->renderSurfels(commandBuffer, currentFrame, glm::mat4(1.0f), 0.0025f);
    }
    if (simulation && flags.drawVoronoi && simulation->simulationActive() && simulation->voronoiReady()) {
        simulation->renderVoronoiSurface(commandBuffer, currentFrame);
    }
    if (simulation && flags.drawPoints && simulation->simulationActive() && simulation->voronoiReady()) {
        simulation->renderOccupancy(commandBuffer, currentFrame, extent);
    }

    if (flags.wireframeMode > 0) {
        VkDescriptorSet geometryDescriptorSet = geometryPass.getDescriptorSet(currentFrame);
        if (geometryDescriptorSet != VK_NULL_HANDLE) {
            std::vector<WireframeRenderer::DrawItem> wireframeItems;
            for (uint32_t modelId : resourceManager.getRenderableModelIds()) {
                Model* model = resourceManager.getModelByID(modelId);
                if (!model) {
                    continue;
                }

                wireframeItems.push_back({ model, model->getModelMatrix() });
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

    if (simulation && flags.drawContactLines) {
        simulation->renderContactLines(commandBuffer, currentFrame, extent);
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


