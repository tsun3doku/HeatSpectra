#pragma once

#include <utility>
#include <vector>

#include "runtime/RuntimePackages.hpp"

class RuntimePackageController;

struct RuntimeSyncPlan {
    std::vector<uint64_t> removeHeatSockets;
    std::vector<uint64_t> removeContactSockets;
    std::vector<uint64_t> removeVoronoiSockets;
    std::vector<uint64_t> removeRemeshSockets;
    std::vector<uint64_t> removeGeometrySockets;

    std::vector<std::pair<uint64_t, GeometryPackage>> applyGeometryPackages;
    std::vector<std::pair<uint64_t, RemeshPackage>> applyRemeshPackages;
    std::vector<std::pair<uint64_t, VoronoiPackage>> applyVoronoiPackages;
    std::vector<std::pair<uint64_t, ContactPackage>> applyContactPackages;
    std::vector<std::pair<uint64_t, HeatPackage>> applyHeatPackages;

    bool empty() const {
        return removeHeatSockets.empty() &&
            removeContactSockets.empty() &&
            removeVoronoiSockets.empty() &&
            removeRemeshSockets.empty() &&
            removeGeometrySockets.empty() &&
            applyGeometryPackages.empty() &&
            applyRemeshPackages.empty() &&
            applyVoronoiPackages.empty() &&
            applyContactPackages.empty() &&
            applyHeatPackages.empty();
    }
};

class RuntimePackageSync {
public:
    RuntimeSyncPlan buildPlan(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next) const;
    void sync(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimePackageController& payloadController) const;

private:
    static bool containsActiveModelId(const RuntimePackageSet& packageSet, uint32_t modelId);
    void removeHeat(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void removeContact(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void removeVoronoi(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void removeRemesh(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void removeGeometry(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void applyGeometry(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void applyRemesh(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void applyVoronoi(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void applyContact(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
    void applyHeat(
        const RuntimePackageSet& previous,
        const RuntimePackageSet& next,
        RuntimeSyncPlan& plan) const;
};
