#pragma once

#include "NodeGraphTypes.hpp"
#include "runtime/RuntimePackageGraph.hpp"

#include <unordered_set>

class NodeGraphDisplay {
public:
    RuntimePackageGraph selectDisplayedSubgraph(
        const NodeGraphState& graphState,
        const RuntimePackageGraph& fullGraph) const;

private:
    void appendRootPackagesForNode(
        const NodeGraphNode& node,
        std::unordered_set<PackageKey, PackageKeyHash>& selectedKeys) const;
    void expandDependencyClosure(
        const RuntimePackageGraph& fullGraph,
        std::unordered_set<PackageKey, PackageKeyHash>& selectedKeys) const;
    RuntimePackageGraph filterGraph(
        const RuntimePackageGraph& fullGraph,
        const std::unordered_set<PackageKey, PackageKeyHash>& selectedKeys) const;
};
