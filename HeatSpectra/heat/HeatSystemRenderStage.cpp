#include "HeatSystemRenderStage.hpp"

#include "HeatSystemResources.hpp"
#include "HeatReceiver.hpp"
#include "HeatSource.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "renderers/ContactLineRenderer.hpp"
#include "renderers/HeatRenderer.hpp"
#include "renderers/PointRenderer.hpp"
#include "renderers/SurfelRenderer.hpp"
#include "renderers/VoronoiRenderer.hpp"
#include "scene/Model.hpp"

#include <glm/glm.hpp>

HeatSystemRenderStage::HeatSystemRenderStage(const HeatSystemStageContext& stageContext)
    : context(stageContext) {
}

void HeatSystemRenderStage::renderContactLines(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent, ContactLineRenderer* contactLineRenderer,
    bool isActive) const {
    if (!contactLineRenderer || !isActive) {
        return;
    }

    contactLineRenderer->render(cmdBuffer, frameIndex, glm::mat4(1.0f), extent);
}

const HeatSystemVoronoiDomain* HeatSystemRenderStage::findReceiverDomainByModelId(
    const std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains,
    uint32_t receiverModelId) const {
    if (receiverModelId == 0) {
        return nullptr;
    }

    for (const HeatSystemVoronoiDomain& domain : receiverVoronoiDomains) {
        if (domain.receiverModelId == receiverModelId) {
            return &domain;
        }
    }

    return nullptr;
}

void HeatSystemRenderStage::renderVoronoiSurface(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VoronoiRenderer* voronoiRenderer,
    const std::vector<std::unique_ptr<HeatReceiver>>& receivers, const std::vector<HeatSystemVoronoiDomain>& receiverVoronoiDomains, bool isActive) const {
    if (!voronoiRenderer || !isActive) {
        return;
    }

    for (const auto& receiver : receivers) {
        const HeatSystemVoronoiDomain* receiverDomain =
            findReceiverDomainByModelId(receiverVoronoiDomains, receiver->getModel().getRuntimeModelId());
        if (!receiverDomain || receiverDomain->nodeCount == 0) {
            continue;
        }

        Model& model = receiver->getModel();
        const uint32_t vertexCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
        const VkBuffer candidateBuffer = receiver->getVoronoiCandidateBuffer();
        if (candidateBuffer == VK_NULL_HANDLE || vertexCount == 0) {
            continue;
        }

        auto* iodt = context.remesher.getRemesherForModel(&model);
        if (!iodt) {
            continue;
        }

        auto* supportingHalfedge = iodt->getSupportingHalfedge();
        if (!supportingHalfedge || !supportingHalfedge->isUploadedToGPU()) {
            continue;
        }

        voronoiRenderer->updateDescriptors(
            frameIndex,
            vertexCount,
            context.resources.seedPositionBuffer,
            context.resources.seedPositionBufferOffset_,
            context.resources.neighborIndicesBuffer,
            context.resources.neighborIndicesBufferOffset_,
            supportingHalfedge->getSupportingHalfedgeView(),
            supportingHalfedge->getSupportingAngleView(),
            supportingHalfedge->getHalfedgeView(),
            supportingHalfedge->getEdgeView(),
            supportingHalfedge->getTriangleView(),
            supportingHalfedge->getLengthView(),
            candidateBuffer,
            receiver->getVoronoiCandidateBufferOffset());

        voronoiRenderer->render(
            cmdBuffer,
            model.getVertexBuffer(),
            model.getVertexBufferOffset(),
            model.getIndexBuffer(),
            model.getIndexBufferOffset(),
            static_cast<uint32_t>(model.getIndices().size()),
            frameIndex,
            model.getModelMatrix());
    }
}

void HeatSystemRenderStage::renderHeatOverlay(VkCommandBuffer cmdBuffer, uint32_t frameIndex, HeatRenderer* heatRenderer,
    const std::vector<HeatSystemRuntime::SourceCoupling>& sourceCouplings, const std::vector<std::unique_ptr<HeatReceiver>>& receivers,
    bool isActive, bool isPaused) const {
    if (!heatRenderer || (!isActive && !isPaused)) {
        return;
    }

    std::vector<HeatRenderer::SourceRenderBinding> sourceRenderBindings;
    sourceRenderBindings.reserve(sourceCouplings.size());
    for (const HeatSystemRuntime::SourceCoupling& sourceCoupling : sourceCouplings) {
        if (!sourceCoupling.model || !sourceCoupling.heatSource) {
            continue;
        }

        HeatRenderer::SourceRenderBinding sourceBinding{};
        sourceBinding.model = sourceCoupling.model;
        sourceBinding.heatSource = sourceCoupling.heatSource.get();
        sourceRenderBindings.push_back(sourceBinding);
    }

    heatRenderer->render(cmdBuffer, frameIndex, sourceRenderBindings, receivers);
}

void HeatSystemRenderStage::renderOccupancy(VkCommandBuffer cmdBuffer, uint32_t frameIndex, VkExtent2D extent,PointRenderer* pointRenderer, bool isActive) const {
    if (!pointRenderer || !isActive) {
        return;
    }

    pointRenderer->render(cmdBuffer, frameIndex, glm::mat4(1.0f), extent);
}

const HeatSystemRuntime::SourceCoupling* HeatSystemRenderStage::findSourceCouplingForModel(const std::vector<HeatSystemRuntime::SourceCoupling>& sourceCouplings,
    const Model* model) const {
    if (!model) {
        return nullptr;
    }

    for (const HeatSystemRuntime::SourceCoupling& sourceCoupling : sourceCouplings) {
        if (sourceCoupling.model == model) {
            return &sourceCoupling;
        }
    }

    return nullptr;
}

void HeatSystemRenderStage::renderSurfels(VkCommandBuffer cmdBuffer, uint32_t frameIndex, float radius,
    const std::vector<HeatSystemRuntime::SourceCoupling>& sourceCouplings, const std::vector<std::unique_ptr<HeatReceiver>>& receivers) const {
    for (Model* model : context.remesher.getRemeshedModels()) {
        SurfelRenderer* surfelRenderer = context.remesher.getSurfelForModel(model);
        if (!surfelRenderer) {
            continue;
        }

        SurfelRenderer::Surfel surfel{};
        surfel.modelMatrix = model->getModelMatrix();
        surfel.surfelRadius = radius;

        VkBuffer surfaceBuffer = VK_NULL_HANDLE;
        VkDeviceSize surfaceBufferOffset = 0;
        uint32_t surfelCount = 0;

        const HeatSystemRuntime::SourceCoupling* sourceCoupling =
            findSourceCouplingForModel(sourceCouplings, model);
        if (sourceCoupling && sourceCoupling->heatSource) {
            surfaceBuffer = sourceCoupling->heatSource->getSourceBuffer();
            surfaceBufferOffset = sourceCoupling->heatSource->getSourceBufferOffset();
            surfelCount = static_cast<uint32_t>(sourceCoupling->heatSource->getVertexCount());
        } else {
            for (const auto& receiver : receivers) {
                if (&receiver->getModel() != model) {
                    continue;
                }

                surfaceBuffer = receiver->getSurfaceBuffer();
                surfaceBufferOffset = receiver->getSurfaceBufferOffset();
                surfelCount = static_cast<uint32_t>(receiver->getIntrinsicVertexCount());
                break;
            }
        }

        if (surfaceBuffer == VK_NULL_HANDLE || surfelCount == 0) {
            continue;
        }

        surfelRenderer->render(cmdBuffer, surfaceBuffer, surfaceBufferOffset, surfelCount, surfel, frameIndex);
    }
}
