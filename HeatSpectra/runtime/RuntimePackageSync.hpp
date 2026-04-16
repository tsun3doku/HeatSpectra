#pragma once

#include <cstdint>
#include <vector>

#include "runtime/RuntimePackageGraph.hpp"

struct PackagePlanEntry {
    PackageKey key{};
};

struct PackagePlanGroup {
    std::vector<PackagePlanEntry> entries;

    bool empty() const {
        return entries.empty();
    }
};

struct PackagePlan {
    std::vector<PackagePlanEntry> removals;
    std::vector<PackagePlanGroup> groups;
    std::vector<PackageNode> blockedNodes;

    bool empty() const {
        return removals.empty() &&
            groups.empty();
    }
};

class RuntimePackageSync {
public:
    template <typename PackageControllerT>
    RuntimePackageGraph sync(const RuntimePackageGraph& previousGraph, const RuntimePackageGraph& currentGraph, PackageControllerT& packageController) const {
        PackagePlan plan{};
        buildRemovePlan(previousGraph.compiledPackages.packageSet, currentGraph.compiledPackages.packageSet, plan);
        packageController.executeRemovals(plan);

        RuntimePackageSet installedPackages = previousGraph.compiledPackages.packageSet;
        for (const PackagePlanEntry& entry : plan.removals) {
            switch (entry.key.kind) {
            case PackageKind::Model:
                installedPackages.modelBySocket.erase(entry.key.outputSocketKey);
                break;
            case PackageKind::Remesh:
                installedPackages.remeshBySocket.erase(entry.key.outputSocketKey);
                break;
            case PackageKind::Voronoi:
                installedPackages.voronoiBySocket.erase(entry.key.outputSocketKey);
                break;
            case PackageKind::Contact:
                installedPackages.contactBySocket.erase(entry.key.outputSocketKey);
                break;
            case PackageKind::Heat:
                installedPackages.heatBySocket.erase(entry.key.outputSocketKey);
                break;
            }
        }

        while (true) {
            std::vector<PackageNode> blockedNodes;
            const PackagePlanGroup group = buildReadyGroup(currentGraph, installedPackages, &blockedNodes);
            if (group.empty()) {
                plan.blockedNodes = blockedNodes;
                break;
            }

            packageController.executeGroup(group, currentGraph.compiledPackages);
            plan.groups.push_back(group);

            for (const PackagePlanEntry& entry : group.entries) {
                copyInstalledPackage(
                    installedPackages,
                    currentGraph.compiledPackages.packageSet,
                    entry.key,
                    entry.key.outputSocketKey);
            }
        }

        return currentGraph;
    }

private:
    static void copyInstalledPackage(RuntimePackageSet& installedPackages, const RuntimePackageSet& sourcePackages, const PackageKey& key, uint64_t socketKey);
    static bool packageMatches(const RuntimePackageSet& packageSet, PackageKind kind, uint64_t outputSocketKey, uint64_t packageHash);
    static bool dependencySatisfied(const PackageDependency& dependency, const RuntimePackageGraph& graph, const RuntimePackageSet& installedPackages);

    void buildRemovePlan(const RuntimePackageSet& previousPackages, const RuntimePackageSet& nextPackages, PackagePlan& plan) const;
    PackagePlanGroup buildReadyGroup(const RuntimePackageGraph& graph, const RuntimePackageSet& installedPackages, std::vector<PackageNode>* blockedNodes) const;
};
