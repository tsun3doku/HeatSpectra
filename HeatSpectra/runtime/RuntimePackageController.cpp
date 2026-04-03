#include "RuntimePackageController.hpp"

#include "nodegraph/NodeModelTransform.hpp"
#include "scene/Model.hpp"
#include "scene/SceneController.hpp"
#include "runtime/RuntimePackageSync.hpp"
#include "runtime/RuntimeContactTransport.hpp"
#include "runtime/RuntimeHeatTransport.hpp"
#include "runtime/RuntimeModelTransport.hpp"
#include "runtime/RuntimeRemeshTransport.hpp"
#include "runtime/RuntimeVoronoiTransport.hpp"
#include "vulkan/ResourceManager.hpp"

RuntimePackageController::RuntimePackageController(ResourceManager& resourceManager)
    : resourceManager(resourceManager) {
}

void RuntimePackageController::setRemeshTransport(RuntimeRemeshTransport* updatedRemeshTransport) {
    remeshTransport = updatedRemeshTransport;
}

void RuntimePackageController::setHeatTransport(RuntimeHeatTransport* updatedHeatTransport) {
    heatTransport = updatedHeatTransport;
}

void RuntimePackageController::setContactTransport(RuntimeContactTransport* updatedContactTransport) {
    contactTransport = updatedContactTransport;
}

void RuntimePackageController::setVoronoiTransport(RuntimeVoronoiTransport* updatedVoronoiTransport) {
    voronoiTransport = updatedVoronoiTransport;
}

void RuntimePackageController::setModelTransport(RuntimeModelTransport* updatedModelTransport) {
    modelTransport = updatedModelTransport;
}

void RuntimePackageController::setSceneController(SceneController* updatedSceneController) {
    sceneController = updatedSceneController;
}

void RuntimePackageController::applyGeometry(uint64_t socketKey, const GeometryPackage& geometryPackage) {
    if (geometryPackage.geometry.modelId != 0) {
        geometryNodeModelIdBySocketKey[socketKey] = geometryPackage.geometry.modelId;
    }

    if (!sceneController || geometryPackage.geometry.modelId == 0) {
        return;
    }

    uint32_t runtimeModelId = 0;
    if (!geometryPackage.geometry.baseModelPath.empty()) {
        runtimeModelId = sceneController->materializeModelSink(
            geometryPackage.geometry.modelId,
            geometryPackage.geometry.baseModelPath);
    } else if (!sceneController->tryGetNodeModelRuntimeId(geometryPackage.geometry.modelId, runtimeModelId)) {
        runtimeModelId = 0;
    }
    if (runtimeModelId == 0) {
        return;
    }

    Model* sinkModel = resourceManager.getModelByID(runtimeModelId);
    if (!sinkModel) {
        return;
    }

    sinkModel->setModelMatrix(NodeModelTransform::toMat4(geometryPackage.geometry.localToWorld));
    if (modelTransport) {
        modelTransport->publish(socketKey, runtimeModelId);
    }
    heatSystemsDirty = true;
}

void RuntimePackageController::removeGeometry(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    const auto it = geometryNodeModelIdBySocketKey.find(socketKey);
    if (it == geometryNodeModelIdBySocketKey.end()) {
        return;
    }

    const uint32_t nodeModelId = it->second;
    geometryNodeModelIdBySocketKey.erase(it);
    if (modelTransport) {
        modelTransport->remove(socketKey);
    }
    if (sceneController && nodeModelId != 0) {
        sceneController->removeNodeModelSink(nodeModelId);
    }
    heatSystemsDirty = true;
}

void RuntimePackageController::applyRemesh(uint64_t socketKey, const RemeshPackage& remeshPackage) {
    if (socketKey == 0) {
        return;
    }

    remeshPackagesBySocket[socketKey] = remeshPackage;
    remeshSystemsDirty = true;
    voronoiSystemsDirty = true;
    heatSystemsDirty = true;
}

void RuntimePackageController::removeRemesh(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    remeshPackagesBySocket.erase(socketKey);
    remeshSystemsDirty = true;
    voronoiSystemsDirty = true;
    heatSystemsDirty = true;
}

void RuntimePackageController::applyHeat(uint64_t socketKey, const HeatPackage& nextHeatPackage) {
    if (socketKey == 0) {
        return;
    }

    heatPackagesBySocket[socketKey] = nextHeatPackage;
    heatSystemsDirty = true;
}

void RuntimePackageController::removeHeat(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    heatPackagesBySocket.erase(socketKey);
    heatSystemsDirty = true;
}

void RuntimePackageController::applyContact(uint64_t socketKey, const ContactPackage& nextContactPackage) {
    if (socketKey == 0) {
        return;
    }

    contactPackagesBySocket[socketKey] = nextContactPackage;
    contactSystemsDirty = true;
    heatSystemsDirty = true;
}

void RuntimePackageController::removeContact(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    contactPackagesBySocket.erase(socketKey);
    contactSystemsDirty = true;
    heatSystemsDirty = true;
}

void RuntimePackageController::applyVoronoi(uint64_t socketKey, const VoronoiPackage& nextVoronoiPackage) {
    if (socketKey == 0) {
        return;
    }

    voronoiPackagesBySocket[socketKey] = nextVoronoiPackage;
    voronoiSystemsDirty = true;
    heatSystemsDirty = true;
}

void RuntimePackageController::removeVoronoi(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    voronoiPackagesBySocket.erase(socketKey);
    voronoiSystemsDirty = true;
    heatSystemsDirty = true;
}

void RuntimePackageController::executePlan(const RuntimeSyncPlan& plan) {
    for (uint64_t socketKey : plan.removeHeatSockets) {
        removeHeat(socketKey);
    }
    for (uint64_t socketKey : plan.removeContactSockets) {
        removeContact(socketKey);
    }
    for (uint64_t socketKey : plan.removeVoronoiSockets) {
        removeVoronoi(socketKey);
    }

    if (heatSystemsDirty && heatTransport) {
        const HeatPackage* activeHeatPackage = selectPackage(heatPackagesBySocket);
        heatTransport->apply(activeHeatPackage);
        heatSystemsDirty = false;
    }
    if (contactSystemsDirty && contactTransport) {
        const auto [activeContactSocketKey, activeContactPackage] = selectPackageEntry(contactPackagesBySocket);
        contactTransport->apply(activeContactSocketKey, activeContactPackage);
        contactSystemsDirty = false;
    }
    if (voronoiSystemsDirty && voronoiTransport) {
        const auto [activeVoronoiSocketKey, activeVoronoiPackage] = selectPackageEntry(voronoiPackagesBySocket);
        voronoiTransport->apply(activeVoronoiSocketKey, activeVoronoiPackage);
        voronoiSystemsDirty = false;
    }

    for (uint64_t socketKey : plan.removeRemeshSockets) {
        removeRemesh(socketKey);
    }
    for (uint64_t socketKey : plan.removeGeometrySockets) {
        removeGeometry(socketKey);
    }
    if (remeshSystemsDirty && remeshTransport) {
        remeshTransport->sync(remeshPackagesBySocket);
        remeshSystemsDirty = false;
    }

    for (const auto& [socketKey, geometryPackage] : plan.applyGeometryPackages) {
        applyGeometry(socketKey, geometryPackage);
    }
    for (const auto& [socketKey, remeshPackage] : plan.applyRemeshPackages) {
        applyRemesh(socketKey, remeshPackage);
    }
    if (remeshSystemsDirty && remeshTransport) {
        remeshTransport->sync(remeshPackagesBySocket);
        remeshSystemsDirty = false;
    }

    for (const auto& [socketKey, voronoiPackage] : plan.applyVoronoiPackages) {
        applyVoronoi(socketKey, voronoiPackage);
    }
    for (const auto& [socketKey, contactPackage] : plan.applyContactPackages) {
        applyContact(socketKey, contactPackage);
    }
    for (const auto& [socketKey, heatPackage] : plan.applyHeatPackages) {
        applyHeat(socketKey, heatPackage);
    }

    syncBackendSystems();
}

void RuntimePackageController::syncBackendSystems() {
    if (voronoiSystemsDirty && voronoiTransport) {
        const auto [activeVoronoiSocketKey, activeVoronoiPackage] = selectPackageEntry(voronoiPackagesBySocket);
        voronoiTransport->apply(activeVoronoiSocketKey, activeVoronoiPackage);
        voronoiSystemsDirty = false;
    }

    if (contactSystemsDirty && contactTransport) {
        const auto [activeContactSocketKey, activeContactPackage] = selectPackageEntry(contactPackagesBySocket);
        contactTransport->apply(activeContactSocketKey, activeContactPackage);
        contactSystemsDirty = false;
    }

    if (heatSystemsDirty && heatTransport) {
        const HeatPackage* activeHeatPackage = selectPackage(heatPackagesBySocket);
        heatTransport->apply(activeHeatPackage);
        heatSystemsDirty = false;
    }
}
