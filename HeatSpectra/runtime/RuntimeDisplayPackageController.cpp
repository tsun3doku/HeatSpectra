#include "RuntimeDisplayPackageController.hpp"

#include "runtime/RuntimePackageSync.hpp"

void RuntimeDisplayPackageController::setModelTransport(RuntimeModelDisplayTransport* updatedModelTransport) {
    modelTransport = updatedModelTransport;
}

void RuntimeDisplayPackageController::setRemeshDisplayTransport(RuntimeRemeshDisplayTransport* updatedRemeshTransport) {
    remeshTransport = updatedRemeshTransport;
}

void RuntimeDisplayPackageController::setHeatDisplayTransport(RuntimeHeatDisplayTransport* updatedHeatTransport) {
    heatTransport = updatedHeatTransport;
}

void RuntimeDisplayPackageController::setContactDisplayTransport(RuntimeContactDisplayTransport* updatedContactTransport) {
    contactTransport = updatedContactTransport;
}

void RuntimeDisplayPackageController::setVoronoiDisplayTransport(RuntimeVoronoiDisplayTransport* updatedVoronoiTransport) {
    voronoiTransport = updatedVoronoiTransport;
}

void RuntimeDisplayPackageController::applyModel(uint64_t socketKey, const ModelPackage& modelPackage) {
    if (socketKey == 0) {
        return;
    }

    modelPackagesBySocket[socketKey] = modelPackage;
    modelSystemsDirty = true;
}

void RuntimeDisplayPackageController::applyRemesh(uint64_t socketKey, const RemeshPackage& remeshPackage) {
    if (socketKey == 0) {
        return;
    }

    remeshPackagesBySocket[socketKey] = remeshPackage;
    remeshSystemsDirty = true;
}

void RuntimeDisplayPackageController::removeModel(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    modelPackagesBySocket.erase(socketKey);
    modelSystemsDirty = true;
}

void RuntimeDisplayPackageController::removeRemesh(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    remeshPackagesBySocket.erase(socketKey);
    remeshSystemsDirty = true;
}

void RuntimeDisplayPackageController::applyHeat(uint64_t socketKey, const HeatPackage& heatPackage) {
    if (socketKey == 0) {
        return;
    }

    heatPackagesBySocket[socketKey] = heatPackage;
    heatSystemsDirty = true;
}

void RuntimeDisplayPackageController::removeHeat(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    heatPackagesBySocket.erase(socketKey);
    heatSystemsDirty = true;
}

void RuntimeDisplayPackageController::applyContact(uint64_t socketKey, const ContactPackage& contactPackage) {
    if (socketKey == 0) {
        return;
    }

    contactPackagesBySocket[socketKey] = contactPackage;
    contactSystemsDirty = true;
}

void RuntimeDisplayPackageController::removeContact(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    contactPackagesBySocket.erase(socketKey);
    contactSystemsDirty = true;
}

void RuntimeDisplayPackageController::applyVoronoi(uint64_t socketKey, const VoronoiPackage& voronoiPackage) {
    if (socketKey == 0) {
        return;
    }

    voronoiPackagesBySocket[socketKey] = voronoiPackage;
    voronoiSystemsDirty = true;
}

void RuntimeDisplayPackageController::removeVoronoi(uint64_t socketKey) {
    if (socketKey == 0) {
        return;
    }

    voronoiPackagesBySocket.erase(socketKey);
    voronoiSystemsDirty = true;
}

void RuntimeDisplayPackageController::executeRemovals(const PackagePlan& plan) {
    for (const PackagePlanEntry& entry : plan.removals) {
        if (entry.key.kind == PackageKind::Model) {
            removeModel(entry.key.outputSocketKey);
        } else if (entry.key.kind == PackageKind::Remesh) {
            removeRemesh(entry.key.outputSocketKey);
        } else if (entry.key.kind == PackageKind::Heat) {
            removeHeat(entry.key.outputSocketKey);
        } else if (entry.key.kind == PackageKind::Contact) {
            removeContact(entry.key.outputSocketKey);
        } else if (entry.key.kind == PackageKind::Voronoi) {
            removeVoronoi(entry.key.outputSocketKey);
        }
    }

    syncBackendSystems();
}

void RuntimeDisplayPackageController::executeGroup(
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
        case PackageKind::Heat: {
            const auto it = compiledPackages.packageSet.heatBySocket.find(entry.key.outputSocketKey);
            if (it != compiledPackages.packageSet.heatBySocket.end()) {
                applyHeat(entry.key.outputSocketKey, it->second);
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
        case PackageKind::Voronoi: {
            const auto it = compiledPackages.packageSet.voronoiBySocket.find(entry.key.outputSocketKey);
            if (it != compiledPackages.packageSet.voronoiBySocket.end()) {
                applyVoronoi(entry.key.outputSocketKey, it->second);
            }
            break;
        }
        default:
            break;
        }
    }

    syncBackendSystems();
}

void RuntimeDisplayPackageController::syncBackendSystems() {
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

    if (heatSystemsDirty && heatTransport) {
        heatTransport->sync(heatPackagesBySocket);
        heatTransport->finalizeSync();
        heatSystemsDirty = false;
    }

    if (contactSystemsDirty && contactTransport) {
        contactTransport->sync(contactPackagesBySocket);
        contactTransport->finalizeSync();
        contactSystemsDirty = false;
    }

    if (voronoiSystemsDirty && voronoiTransport) {
        voronoiTransport->sync(voronoiPackagesBySocket);
        voronoiTransport->finalizeSync();
        voronoiSystemsDirty = false;
    }
}
