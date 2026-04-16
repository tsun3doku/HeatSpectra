#include "RuntimePackageSync.hpp"

void RuntimePackageSync::copyInstalledPackage(
    RuntimePackageSet& installedPackages,
    const RuntimePackageSet& sourcePackages,
    const PackageKey& key,
    uint64_t socketKey) {
    switch (key.kind) {
    case PackageKind::Model: {
        const auto it = sourcePackages.modelBySocket.find(socketKey);
        if (it != sourcePackages.modelBySocket.end()) {
            installedPackages.modelBySocket[socketKey] = it->second;
        }
        break;
    }
    case PackageKind::Remesh: {
        const auto it = sourcePackages.remeshBySocket.find(socketKey);
        if (it != sourcePackages.remeshBySocket.end()) {
            installedPackages.remeshBySocket[socketKey] = it->second;
        }
        break;
    }
    case PackageKind::Voronoi: {
        const auto it = sourcePackages.voronoiBySocket.find(socketKey);
        if (it != sourcePackages.voronoiBySocket.end()) {
            installedPackages.voronoiBySocket[socketKey] = it->second;
        }
        break;
    }
    case PackageKind::Contact: {
        const auto it = sourcePackages.contactBySocket.find(socketKey);
        if (it != sourcePackages.contactBySocket.end()) {
            installedPackages.contactBySocket[socketKey] = it->second;
        }
        break;
    }
    case PackageKind::Heat: {
        const auto it = sourcePackages.heatBySocket.find(socketKey);
        if (it != sourcePackages.heatBySocket.end()) {
            installedPackages.heatBySocket[socketKey] = it->second;
        }
        break;
    }
    }
}

bool RuntimePackageSync::packageMatches(
    const RuntimePackageSet& packageSet,
    PackageKind kind,
    uint64_t outputSocketKey,
    uint64_t packageHash) {
    switch (kind) {
    case PackageKind::Model: {
        const auto it = packageSet.modelBySocket.find(outputSocketKey);
        return it != packageSet.modelBySocket.end() &&
            it->second.packageHash == packageHash;
    }
    case PackageKind::Remesh: {
        const auto it = packageSet.remeshBySocket.find(outputSocketKey);
        return it != packageSet.remeshBySocket.end() &&
            it->second.packageHash == packageHash;
    }
    case PackageKind::Voronoi: {
        const auto it = packageSet.voronoiBySocket.find(outputSocketKey);
        return it != packageSet.voronoiBySocket.end() &&
            it->second.packageHash == packageHash;
    }
    case PackageKind::Contact: {
        const auto it = packageSet.contactBySocket.find(outputSocketKey);
        return it != packageSet.contactBySocket.end() &&
            it->second.packageHash == packageHash;
    }
    case PackageKind::Heat: {
        const auto it = packageSet.heatBySocket.find(outputSocketKey);
        return it != packageSet.heatBySocket.end() &&
            it->second.packageHash == packageHash;
    }
    }

    return false;
}

bool RuntimePackageSync::dependencySatisfied(
    const PackageDependency& dependency,
    const RuntimePackageGraph& graph,
    const RuntimePackageSet& installedPackages) {
    const PackageNode* producer = graph.findProducer(dependency.productType, dependency.outputSocketKey);
    if (!producer) {
        return false;
    }

    return packageMatches(
        installedPackages,
        producer->key.kind,
        producer->key.outputSocketKey,
        producer->packageHash);
}

void RuntimePackageSync::buildRemovePlan(
    const RuntimePackageSet& previousPackages,
    const RuntimePackageSet& nextPackages,
    PackagePlan& plan) const {
    for (const auto& entry : previousPackages.heatBySocket) {
        const auto nextIt = nextPackages.heatBySocket.find(entry.first);
        if (nextIt != nextPackages.heatBySocket.end() &&
            entry.second.matches(nextIt->second)) {
            continue;
        }

        plan.removals.push_back(PackagePlanEntry{PackageKey{PackageKind::Heat, entry.first}});
    }

    for (const auto& entry : previousPackages.contactBySocket) {
        const auto nextIt = nextPackages.contactBySocket.find(entry.first);
        if (nextIt != nextPackages.contactBySocket.end() &&
            entry.second.matches(nextIt->second)) {
            continue;
        }

        plan.removals.push_back(PackagePlanEntry{PackageKey{PackageKind::Contact, entry.first}});
    }

    for (const auto& entry : previousPackages.voronoiBySocket) {
        const auto nextIt = nextPackages.voronoiBySocket.find(entry.first);
        if (nextIt != nextPackages.voronoiBySocket.end() &&
            entry.second.matches(nextIt->second)) {
            continue;
        }

        plan.removals.push_back(PackagePlanEntry{PackageKey{PackageKind::Voronoi, entry.first}});
    }

    for (const auto& entry : previousPackages.remeshBySocket) {
        const auto nextIt = nextPackages.remeshBySocket.find(entry.first);
        if (nextIt != nextPackages.remeshBySocket.end() &&
            entry.second.matches(nextIt->second)) {
            continue;
        }

        plan.removals.push_back(PackagePlanEntry{PackageKey{PackageKind::Remesh, entry.first}});
    }

    for (const auto& entry : previousPackages.modelBySocket) {
        const auto nextIt = nextPackages.modelBySocket.find(entry.first);
        if (nextIt != nextPackages.modelBySocket.end() &&
            entry.second.matches(nextIt->second)) {
            continue;
        }

        plan.removals.push_back(PackagePlanEntry{PackageKey{PackageKind::Model, entry.first}});
    }
}

PackagePlanGroup RuntimePackageSync::buildReadyGroup(
    const RuntimePackageGraph& graph,
    const RuntimePackageSet& installedPackages,
    std::vector<PackageNode>* blockedNodes) const {
    PackagePlanGroup group{};

    for (const PackageNode& node : graph.nodes) {
        if (packageMatches(installedPackages, node.key.kind, node.key.outputSocketKey, node.packageHash)) {
            continue;
        }

        bool dependenciesReady = true;
        for (const PackageDependency& dependency : node.dependencies) {
            if (!dependencySatisfied(dependency, graph, installedPackages)) {
                dependenciesReady = false;
                break;
            }
        }

        if (!dependenciesReady) {
            if (blockedNodes) {
                blockedNodes->push_back(node);
            }
            continue;
        }

        group.entries.push_back(PackagePlanEntry{node.key});
    }

    return group;
}
