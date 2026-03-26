#include "RuntimePayloadController.hpp"

#include "app/SwapchainManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "contact/ContactSystemController.hpp"
#include "heat/HeatSystemController.hpp"
#include "heat/VoronoiSystemController.hpp"
#include "render/RenderRuntime.hpp"
#include "render/SceneRenderer.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "scene/Model.hpp"
#include "scene/SceneController.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "runtime/RuntimeIntrinsicCache.hpp"
#include "vulkan/ResourceManager.hpp"
#include "vulkan/VulkanDevice.hpp"

RuntimePayloadController::RuntimePayloadController(
    VulkanDevice& vulkanDevice,
    SwapchainManager& swapchainManager,
    ResourceManager& resourceManager,
    RuntimeIntrinsicCache& runtimeIntrinsicCache,
    RenderRuntime& renderRuntime,
    std::atomic<bool>& isOperating)
    : vulkanDevice(vulkanDevice),
      swapchainManager(swapchainManager),
      resourceManager(resourceManager),
      runtimeIntrinsicCache(runtimeIntrinsicCache),
      renderRuntime(renderRuntime),
      isOperating(isOperating) {
}

void RuntimePayloadController::setHeatSystemController(HeatSystemController* updatedHeatSystemController) {
    heatSystemController = updatedHeatSystemController;
}

void RuntimePayloadController::setContactSystemController(ContactSystemController* updatedContactSystemController) {
    contactSystemController = updatedContactSystemController;
}

void RuntimePayloadController::setVoronoiSystemController(VoronoiSystemController* updatedVoronoiSystemController) {
    voronoiSystemController = updatedVoronoiSystemController;
}

void RuntimePayloadController::setSceneController(SceneController* updatedSceneController) {
    sceneController = updatedSceneController;
    packageCompiler.setSceneController(updatedSceneController);
}

uint32_t RuntimePayloadController::materializeRuntimeModelSink(const GeometryData& geometry) {
    if (!sceneController || geometry.modelId == 0) {
        return 0;
    }

    if (!geometry.baseModelPath.empty()) {
        return sceneController->materializeModelSink(geometry.modelId, geometry.baseModelPath);
    }

    uint32_t runtimeModelId = 0;
    if (!sceneController->tryGetNodeModelRuntimeId(geometry.modelId, runtimeModelId)) {
        return 0;
    }
    return runtimeModelId;
}

bool RuntimePayloadController::resolveRuntimeModel(const GeometryData& geometry, uint32_t& outRuntimeModelId) const {
    return packageCompiler.resolveRuntimeModel(geometry, outRuntimeModelId);
}

bool RuntimePayloadController::areVoronoiPackagesEquivalent(const VoronoiPackage& lhs, const VoronoiPackage& rhs) {
    return lhs.authored.active == rhs.authored.active &&
        lhs.authored.params == rhs.authored.params &&
        lhs.receiverGeometryHandles == rhs.receiverGeometryHandles &&
        lhs.receiverRuntimeModelIds == rhs.receiverRuntimeModelIds;
}

void RuntimePayloadController::applyHeatPackageToSystem() {
    if (heatSystemController) {
        heatSystemController->applyHeatPackage(heatPackage);
    }
}

void RuntimePayloadController::applyVoronoiPackageToSystem() {
    if (voronoiSystemController) {
        voronoiSystemController->applyVoronoiPackage(voronoiPackage);
    }
}

void RuntimePayloadController::rebuildHeatSystemSinks() {
    if (!heatSystemController) {
        return;
    }

    heatSystemController->recreateHeatSystem(
        swapchainManager.getExtent(),
        renderRuntime.getFrameGraphRuntime().getRenderPass());

    HeatSystem* heatSystem = heatSystemController->getHeatSystem();
    VoronoiSystem* voronoiSystem = nullptr;
    if (voronoiSystemController) {
        voronoiSystem = voronoiSystemController->getVoronoiSystem();
    }
    renderRuntime.setSystems(heatSystem, voronoiSystem);
}

void RuntimePayloadController::applyGeometryPayload(const GeometryData& geometry) {
    GeometryPackage package = packageCompiler.buildGeometryPackage(geometry);
    if (package.geometry.modelId != 0) {
        geometryPackagesByNodeModelId[package.geometry.modelId] = package;
    }

    const uint32_t runtimeModelId = materializeRuntimeModelSink(package.geometry);
    if (runtimeModelId == 0) {
        return;
    }

    Model* sinkModel = resourceManager.getModelByID(runtimeModelId);
    if (!sinkModel) {
        return;
    }

    sinkModel->setModelMatrix(NodeModelTransform::toMat4(package.geometry.localToWorld));
}

void RuntimePayloadController::removeGeometryPayload(uint32_t nodeModelId) {
    if (nodeModelId == 0) {
        return;
    }

    geometryPackagesByNodeModelId.erase(nodeModelId);

    if (!sceneController) {
        return;
    }

    sceneController->removeNodeModelSink(nodeModelId);
}

void RuntimePayloadController::applyRemeshPayload(const GeometryData& geometry, const IntrinsicMeshData& intrinsic) {
    if (geometry.intrinsicHandle.key == 0) {
        return;
    }

    vkDeviceWaitIdle(vulkanDevice.getDevice());
    OperatingScope operatingScope(isOperating);

    const bool intrinsicMaterialized = runtimeIntrinsicCache.apply(geometry.intrinsicHandle, intrinsic);
    if (!intrinsicMaterialized) {
        return;
    }

    Model* sinkModel = nullptr;
    const uint32_t runtimeModelId = materializeRuntimeModelSink(geometry);
    if (runtimeModelId != 0) {
        sinkModel = resourceManager.getModelByID(runtimeModelId);
    }
    if (!sinkModel) {
        return;
    }

    renderRuntime.getSceneRenderer().updateIntrinsicPayloadForModel(sinkModel, intrinsic);

    if (contactSystemController) {
        contactSystemController->clearCache();
    }

    // HeatSystem cleanup dereferences the VoronoiSystem it was built against, so
    // the old HeatSystem must be destroyed before replacing the VoronoiSystem.
    if (heatSystemController) {
        heatSystemController->destroyHeatSystem();
    }

    if (voronoiSystemController) {
        voronoiSystemController->recreateVoronoiSystem(
            swapchainManager.getExtent(),
            renderRuntime.getFrameGraphRuntime().getRenderPass());
    }
    rebuildHeatSystemSinks();
    if (sceneController) {
        sceneController->focusOnVisibleModel();
    }
}

void RuntimePayloadController::removeIntrinsicPayload(const NodeDataHandle& intrinsicHandle) {
    runtimeIntrinsicCache.remove(intrinsicHandle);
}

void RuntimePayloadController::applyHeatPayload(const HeatData& heat) {
    heatPackage = packageCompiler.buildHeatPackage(nullptr, heat, &voronoiPackage.authored);
    hasProjectedHeatPayload = true;
    applyHeatPackageToSystem();
    VoronoiPackage nextVoronoiPackage = packageCompiler.buildVoronoiPackage(nullptr, voronoiPackage.authored);
    if (!areVoronoiPackagesEquivalent(voronoiPackage, nextVoronoiPackage)) {
        voronoiPackage = std::move(nextVoronoiPackage);
        applyVoronoiPackageToSystem();
    }
}

void RuntimePayloadController::applyHeatPayload(const NodePayloadRegistry& payloadRegistry, const HeatData& heat) {
    heatPackage = packageCompiler.buildHeatPackage(&payloadRegistry, heat, &voronoiPackage.authored);
    hasProjectedHeatPayload = true;
    applyHeatPackageToSystem();
    VoronoiPackage nextVoronoiPackage = packageCompiler.buildVoronoiPackage(&payloadRegistry, voronoiPackage.authored);
    if (!areVoronoiPackagesEquivalent(voronoiPackage, nextVoronoiPackage)) {
        voronoiPackage = std::move(nextVoronoiPackage);
        applyVoronoiPackageToSystem();
    }
}

void RuntimePayloadController::applyContactPayload(const ContactData& contact) {
    contactPackage = packageCompiler.buildContactPackage(nullptr, contact);
    if (contactSystemController) {
        contactSystemController->applyContactPackage(contactPackage);
    }
    if (heatSystemController) {
        heatSystemController->applyResolvedContacts(
            contactSystemController ? contactSystemController->getRuntime().getResolvedContacts() : std::vector<RuntimeContactResult>{});
    }
    if (hasProjectedHeatPayload) {
        applyHeatPackageToSystem();
    }
}

void RuntimePayloadController::applyContactPayload(const NodePayloadRegistry& payloadRegistry, const ContactData& contact) {
    contactPackage = packageCompiler.buildContactPackage(&payloadRegistry, contact);
    if (contactSystemController) {
        contactSystemController->applyContactPackage(contactPackage);
    }
    if (heatSystemController) {
        heatSystemController->applyResolvedContacts(
            contactSystemController ? contactSystemController->getRuntime().getResolvedContacts() : std::vector<RuntimeContactResult>{});
    }
    if (hasProjectedHeatPayload) {
        applyHeatPackageToSystem();
    }
}

void RuntimePayloadController::applyVoronoiPayload(const VoronoiData& voronoi) {
    heatPackage = packageCompiler.buildHeatPackage(nullptr, heatPackage.authored, &voronoi);
    applyHeatPackageToSystem();

    VoronoiPackage nextVoronoiPackage = packageCompiler.buildVoronoiPackage(nullptr, voronoi);
    if (!areVoronoiPackagesEquivalent(voronoiPackage, nextVoronoiPackage)) {
        voronoiPackage = std::move(nextVoronoiPackage);
        applyVoronoiPackageToSystem();
    }
}

void RuntimePayloadController::applyVoronoiPayload(const NodePayloadRegistry& payloadRegistry, const VoronoiData& voronoi) {
    heatPackage = packageCompiler.buildHeatPackage(&payloadRegistry, heatPackage.authored, &voronoi);
    applyHeatPackageToSystem();

    VoronoiPackage nextVoronoiPackage = packageCompiler.buildVoronoiPackage(&payloadRegistry, voronoi);
    if (!areVoronoiPackagesEquivalent(voronoiPackage, nextVoronoiPackage)) {
        voronoiPackage = std::move(nextVoronoiPackage);
        applyVoronoiPackageToSystem();
    }
}

RuntimePayloadController::OperatingScope::OperatingScope(std::atomic<bool>& isOperating)
    : isOperating(isOperating) {
    previousState = isOperating.exchange(true, std::memory_order_acq_rel);
}

RuntimePayloadController::OperatingScope::~OperatingScope() {
    isOperating.store(previousState, std::memory_order_release);
}
