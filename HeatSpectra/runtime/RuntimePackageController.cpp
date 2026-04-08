#include "RuntimePackageController.hpp"

#include "runtime/ModelRuntime.hpp"
#include "nodegraph/NodeModelTransform.hpp"
#include "runtime/RuntimePackageSync.hpp"
#include "runtime/RuntimeContactTransport.hpp"
#include "runtime/RuntimeHeatTransport.hpp"
#include "runtime/RuntimeModelTransport.hpp"
#include "runtime/RuntimeRemeshTransport.hpp"
#include "runtime/RuntimeVoronoiTransport.hpp"

RuntimePackageController::RuntimePackageController(ModelRuntime& modelRuntime)
    : modelRuntime(modelRuntime) {
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

void RuntimePackageController::applyGeometry(uint64_t socketKey, const GeometryPackage& geometryPackage) {
    if (geometryPackage.geometry.modelId != 0) {
        geometryNodeModelIdBySocketKey[socketKey] = geometryPackage.geometry.modelId;
    }

    if (geometryPackage.geometry.modelId == 0) {
        return;
    }

    modelRuntime.queueApplySink(
        geometryPackage.geometry.modelId,
        geometryPackage.geometry.baseModelPath,
        NodeModelTransform::toMat4(geometryPackage.geometry.localToWorld));
    pendingGeometryModelPublishes.emplace_back(socketKey, geometryPackage.geometry.modelId);
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
    pendingGeometryModelRemovals.push_back(socketKey);
    if (nodeModelId != 0) {
        modelRuntime.queueRemoveSink(nodeModelId);
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
    pendingGeometryModelRemovals.clear();
    pendingGeometryModelPublishes.clear();

    for (uint64_t socketKey : plan.removeHeatSockets) {
        removeHeat(socketKey);
    }
    for (uint64_t socketKey : plan.removeContactSockets) {
        removeContact(socketKey);
    }
    for (uint64_t socketKey : plan.removeVoronoiSockets) {
        removeVoronoi(socketKey);
    }

    for (uint64_t socketKey : plan.removeRemeshSockets) {
        removeRemesh(socketKey);
    }
    for (uint64_t socketKey : plan.removeGeometrySockets) {
        removeGeometry(socketKey);
    }

    for (const auto& [socketKey, geometryPackage] : plan.applyGeometryPackages) {
        applyGeometry(socketKey, geometryPackage);
    }
    for (const auto& [socketKey, remeshPackage] : plan.applyRemeshPackages) {
        applyRemesh(socketKey, remeshPackage);
    }
    if (remeshSystemsDirty && remeshTransport) {
        remeshTransport->sync(remeshPackagesBySocket);
    }

    flushQueuedModelOperations();
    if (remeshSystemsDirty && remeshTransport) {
        remeshTransport->finalizeSync();
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

void RuntimePackageController::flushQueuedModelOperations() {
    modelRuntime.flush();

    if (modelTransport) {
        for (uint64_t socketKey : pendingGeometryModelRemovals) {
            modelTransport->remove(socketKey);
        }

        for (const auto& [socketKey, nodeModelId] : pendingGeometryModelPublishes) {
            uint32_t runtimeModelId = 0;
            if (modelRuntime.tryGetRuntimeModelId(nodeModelId, runtimeModelId) && runtimeModelId != 0) {
                modelTransport->publish(socketKey, runtimeModelId);
            } else {
                modelTransport->remove(socketKey);
            }
        }
    }

    pendingGeometryModelRemovals.clear();
    pendingGeometryModelPublishes.clear();
}

void RuntimePackageController::syncBackendSystems() {
    if (voronoiSystemsDirty && voronoiTransport) {
        voronoiTransport->sync(voronoiPackagesBySocket);
        voronoiSystemsDirty = false;
    }

    if (contactSystemsDirty && contactTransport) {
        contactTransport->sync(contactPackagesBySocket);
        contactSystemsDirty = false;
    }

    if (heatSystemsDirty && heatTransport) {
        heatTransport->sync(heatPackagesBySocket);
        heatSystemsDirty = false;
    }
}
