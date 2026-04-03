#include "RuntimePackageSync.hpp"

#include "runtime/RuntimePackageController.hpp"

RuntimeSyncPlan RuntimePackageSync::buildPlan(
    const RuntimePackageSet& previous,
    const RuntimePackageSet& next) const {
    RuntimeSyncPlan plan{};
    removeHeat(previous, next, plan);
    removeContact(previous, next, plan);
    removeVoronoi(previous, next, plan);
    removeRemesh(previous, next, plan);
    removeGeometry(previous, next, plan);

    applyGeometry(previous, next, plan);
    applyRemesh(previous, next, plan);
    applyVoronoi(previous, next, plan);
    applyContact(previous, next, plan);
    applyHeat(previous, next, plan);
    return plan;
}

void RuntimePackageSync::sync(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimePackageController& payloadController) const {
    payloadController.executePlan(buildPlan(previous, next));
}

bool RuntimePackageSync::containsActiveModelId(const RuntimePackageSet& packageSet, uint32_t modelId) {
    if (modelId == 0) {
        return false;
    }

    for (const auto& [socketKey, geometryPackage] : packageSet.geometryBySocket) {
        (void)socketKey;
        if (geometryPackage.geometry.modelId == modelId) {
            return true;
        }
    }

    return false;
}

void RuntimePackageSync::removeHeat(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, heatPackage] : previous.heatBySocket) {
        const auto nextIt = next.heatBySocket.find(socketKey);
        if (nextIt != next.heatBySocket.end() &&
            heatPackage.matches(nextIt->second)) {
            continue;
        }

        plan.removeHeatSockets.push_back(socketKey);
    }
}

void RuntimePackageSync::removeContact(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, contactPackage] : previous.contactBySocket) {
        const auto nextIt = next.contactBySocket.find(socketKey);
        if (nextIt != next.contactBySocket.end() &&
            contactPackage.matches(nextIt->second)) {
            continue;
        }

        plan.removeContactSockets.push_back(socketKey);
    }
}

void RuntimePackageSync::removeVoronoi(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, voronoiPackage] : previous.voronoiBySocket) {
        const auto nextIt = next.voronoiBySocket.find(socketKey);
        if (nextIt != next.voronoiBySocket.end() &&
            voronoiPackage.matches(nextIt->second)) {
            continue;
        }

        plan.removeVoronoiSockets.push_back(socketKey);
    }
}

void RuntimePackageSync::removeRemesh(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, remeshPackage] : previous.remeshBySocket) {
        const auto nextIt = next.remeshBySocket.find(socketKey);
        if (nextIt != next.remeshBySocket.end() &&
            remeshPackage.matches(nextIt->second)) {
            continue;
        }

        plan.removeRemeshSockets.push_back(socketKey);
    }
}

void RuntimePackageSync::removeGeometry(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, geometryPackage] : previous.geometryBySocket) {
        const auto nextIt = next.geometryBySocket.find(socketKey);
        if (nextIt != next.geometryBySocket.end() &&
            geometryPackage.matches(nextIt->second)) {
            continue;
        }

        const uint32_t modelId = geometryPackage.geometry.modelId;
        if (!containsActiveModelId(next, modelId)) {
            plan.removeGeometrySockets.push_back(socketKey);
        }
    }
}

void RuntimePackageSync::applyGeometry(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, geometryPackage] : next.geometryBySocket) {
        const auto previousIt = previous.geometryBySocket.find(socketKey);
        if (previousIt != previous.geometryBySocket.end() &&
            previousIt->second.matches(geometryPackage)) {
            continue;
        }

        plan.applyGeometryPackages.emplace_back(socketKey, geometryPackage);
    }
}

void RuntimePackageSync::applyRemesh(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, remeshPackage] : next.remeshBySocket) {
        const auto previousIt = previous.remeshBySocket.find(socketKey);
        if (previousIt != previous.remeshBySocket.end() &&
            previousIt->second.matches(remeshPackage)) {
            continue;
        }

        plan.applyRemeshPackages.emplace_back(socketKey, remeshPackage);
    }
}

void RuntimePackageSync::applyVoronoi(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, voronoiPackage] : next.voronoiBySocket) {
        const auto previousIt = previous.voronoiBySocket.find(socketKey);
        if (previousIt != previous.voronoiBySocket.end() &&
            previousIt->second.matches(voronoiPackage)) {
            continue;
        }

        plan.applyVoronoiPackages.emplace_back(socketKey, voronoiPackage);
    }
}

void RuntimePackageSync::applyContact(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, contactPackage] : next.contactBySocket) {
        const auto previousIt = previous.contactBySocket.find(socketKey);
        if (previousIt != previous.contactBySocket.end() &&
            previousIt->second.matches(contactPackage)) {
            continue;
        }

        plan.applyContactPackages.emplace_back(socketKey, contactPackage);
    }
}

void RuntimePackageSync::applyHeat(const RuntimePackageSet& previous, const RuntimePackageSet& next, RuntimeSyncPlan& plan) const {
    for (const auto& [socketKey, heatPackage] : next.heatBySocket) {
        const auto previousIt = previous.heatBySocket.find(socketKey);
        if (previousIt != previous.heatBySocket.end() &&
            previousIt->second.matches(heatPackage)) {
            continue;
        }

        plan.applyHeatPackages.emplace_back(socketKey, heatPackage);
    }
}
