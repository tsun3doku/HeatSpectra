#include "RuntimeComputePackageController.hpp"

#include "runtime/RuntimePackageSync.hpp"

void RuntimeComputePackageController::setModelTransport(RuntimeModelComputeTransport* updatedModelTransport) {
    modelTransport = updatedModelTransport;
}

void RuntimeComputePackageController::setRemeshTransport(RuntimeRemeshComputeTransport* updatedRemeshTransport) {
    remeshTransport = updatedRemeshTransport;
}

void RuntimeComputePackageController::setHeatTransport(RuntimeHeatComputeTransport* updatedHeatTransport) {
    heatTransport = updatedHeatTransport;
}

void RuntimeComputePackageController::setContactComputeTransport(RuntimeContactComputeTransport* updatedContactComputeTransport) {
    contactComputeTransport = updatedContactComputeTransport;
}

void RuntimeComputePackageController::setVoronoiComputeTransport(RuntimeVoronoiComputeTransport* updatedVoronoiComputeTransport) {
    voronoiComputeTransport = updatedVoronoiComputeTransport;
}

void RuntimeComputePackageController::applyModel(uint64_t socketKey, const ModelPackage& modelPackage) {
    if (socketKey == 0) {
        return;
    }

    modelPackagesBySocket[socketKey] = modelPackage;
    modelSystemsDirty = true;
}

void RuntimeComputePackageController::removeModel(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    modelPackagesBySocket.erase(socketKey);
    modelSystemsDirty = true;
}

void RuntimeComputePackageController::applyRemesh(uint64_t socketKey, const RemeshPackage& remeshPackage) {
    if (socketKey == 0) {
        return;
    }

    remeshPackagesBySocket[socketKey] = remeshPackage;
    remeshSystemsDirty = true;
}

void RuntimeComputePackageController::removeRemesh(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    remeshPackagesBySocket.erase(socketKey);
    remeshSystemsDirty = true;
}

void RuntimeComputePackageController::applyHeat(uint64_t socketKey, const HeatPackage& nextHeatPackage) {
    if (socketKey == 0) {
        return;
    }

    heatPackagesBySocket[socketKey] = nextHeatPackage;
    heatSystemsDirty = true;
}

void RuntimeComputePackageController::removeHeat(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    heatPackagesBySocket.erase(socketKey);
    heatSystemsDirty = true;
}

void RuntimeComputePackageController::applyContact(uint64_t socketKey, const ContactPackage& nextContactPackage) {
    if (socketKey == 0) {
        return;
    }

    contactPackagesBySocket[socketKey] = nextContactPackage;
    contactSystemsDirty = true;
}

void RuntimeComputePackageController::removeContact(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    contactPackagesBySocket.erase(socketKey);
    contactSystemsDirty = true;
}

void RuntimeComputePackageController::applyVoronoi(uint64_t socketKey, const VoronoiPackage& nextVoronoiPackage) {
    if (socketKey == 0) {
        return;
    }

    voronoiPackagesBySocket[socketKey] = nextVoronoiPackage;
    voronoiSystemsDirty = true;
}

void RuntimeComputePackageController::removeVoronoi(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    voronoiPackagesBySocket.erase(socketKey);
    voronoiSystemsDirty = true;
}

void RuntimeComputePackageController::executeRemovals(const PackagePlan& plan) {
    for (const PackagePlanEntry& entry : plan.removals) {
        switch (entry.key.kind) {
        case PackageKind::Model:
            removeModel(entry.key.outputSocketKey);
            break;
        case PackageKind::Remesh:
            removeRemesh(entry.key.outputSocketKey);
            break;
        case PackageKind::Voronoi:
            removeVoronoi(entry.key.outputSocketKey);
            break;
        case PackageKind::Contact:
            removeContact(entry.key.outputSocketKey);
            break;
        case PackageKind::Heat:
            removeHeat(entry.key.outputSocketKey);
            break;
        }
    }

    syncBackendSystems();
}

void RuntimeComputePackageController::executeGroup(
    const PackagePlanGroup& group,
    const CompiledPackages& compiledPackages) {
    for (const PackagePlanEntry& entry : group.entries) {
        switch (entry.key.kind) {
        case PackageKind::Model: {
            const auto it = compiledPackages.packageSet.modelBySocket.find(entry.key.outputSocketKey);
            if (it != compiledPackages.packageSet.modelBySocket.end()) {
                applyModel(entry.key.outputSocketKey, it->second);
            }
            break;
        }
        case PackageKind::Remesh: {
            const auto it = compiledPackages.packageSet.remeshBySocket.find(entry.key.outputSocketKey);
            if (it != compiledPackages.packageSet.remeshBySocket.end()) {
                applyRemesh(entry.key.outputSocketKey, it->second);
            }
            break;
        }
        case PackageKind::Voronoi: {
            const auto it = compiledPackages.packageSet.voronoiBySocket.find(entry.key.outputSocketKey);
            if (it != compiledPackages.packageSet.voronoiBySocket.end()) {
                applyVoronoi(entry.key.outputSocketKey, it->second);
            }
            break;
        }
        case PackageKind::Contact: {
            const auto it = compiledPackages.packageSet.contactBySocket.find(entry.key.outputSocketKey);
            if (it != compiledPackages.packageSet.contactBySocket.end()) {
                applyContact(entry.key.outputSocketKey, it->second);
            }
            break;
        }
        case PackageKind::Heat: {
            const auto it = compiledPackages.packageSet.heatBySocket.find(entry.key.outputSocketKey);
            if (it != compiledPackages.packageSet.heatBySocket.end()) {
                applyHeat(entry.key.outputSocketKey, it->second);
            }
            break;
        }
        }
    }

    syncBackendSystems();
}

void RuntimeComputePackageController::syncBackendSystems() {
    if (modelSystemsDirty && modelTransport) {
        modelTransport->sync(modelPackagesBySocket);
        modelTransport->finalizeSync();
        modelSystemsDirty = false;
    }

    if (remeshSystemsDirty && remeshTransport) {
        remeshTransport->sync(remeshPackagesBySocket);
        remeshTransport->finalizeSync();
        remeshSystemsDirty = false;
    }

    if (voronoiSystemsDirty && voronoiComputeTransport) {
        voronoiComputeTransport->sync(voronoiPackagesBySocket);
        voronoiComputeTransport->finalizeSync();
        voronoiSystemsDirty = false;
    }

    if (contactSystemsDirty && contactComputeTransport) {
        contactComputeTransport->sync(contactPackagesBySocket);
        contactComputeTransport->finalizeSync();
        contactSystemsDirty = false;
    }

    if (heatSystemsDirty && heatTransport) {
        heatTransport->sync(heatPackagesBySocket);
        heatTransport->finalizeSync();
        heatSystemsDirty = false;
    }
}
