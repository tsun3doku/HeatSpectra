#include "NodeGraphDisplay.hpp"

#include "NodeGraphUtils.hpp"

#include <optional>
#include <unordered_set>
#include <vector>

namespace {

std::optional<PackageKind> displayPackageKindForPayload(NodePayloadType payloadType) {
    switch (payloadType) {
    case NodePayloadType::Geometry:
        return PackageKind::Model;
    case NodePayloadType::Remesh:
        return PackageKind::Remesh;
    case NodePayloadType::Voronoi:
        return PackageKind::Voronoi;
    case NodePayloadType::Contact:
        return PackageKind::Contact;
    case NodePayloadType::Heat:
        return PackageKind::Heat;
    default:
        return std::nullopt;
    }
}

uint64_t displayPackageHashForNode(
    const RuntimePackageGraph& fullGraph,
    const PackageNode& node) {
    switch (node.key.kind) {
    case PackageKind::Remesh: {
        const auto it = fullGraph.compiledPackages.packageSet.remeshBySocket.find(node.key.outputSocketKey);
        return it != fullGraph.compiledPackages.packageSet.remeshBySocket.end()
            ? it->second.displayPackageHash
            : node.packageHash;
    }
    case PackageKind::Voronoi: {
        const auto it = fullGraph.compiledPackages.packageSet.voronoiBySocket.find(node.key.outputSocketKey);
        return it != fullGraph.compiledPackages.packageSet.voronoiBySocket.end()
            ? it->second.displayPackageHash
            : node.packageHash;
    }
    case PackageKind::Contact: {
        const auto it = fullGraph.compiledPackages.packageSet.contactBySocket.find(node.key.outputSocketKey);
        return it != fullGraph.compiledPackages.packageSet.contactBySocket.end()
            ? it->second.displayPackageHash
            : node.packageHash;
    }
    case PackageKind::Heat: {
        const auto it = fullGraph.compiledPackages.packageSet.heatBySocket.find(node.key.outputSocketKey);
        return it != fullGraph.compiledPackages.packageSet.heatBySocket.end()
            ? it->second.displayPackageHash
            : node.packageHash;
    }
    case PackageKind::Model:
    default:
        return node.packageHash;
    }
}

template <typename TPackage>
void copySelectedPackages(
    const std::unordered_map<uint64_t, TPackage>& source,
    PackageKind kind,
    const std::unordered_set<PackageKey, PackageKeyHash>& selectedKeys,
    std::unordered_map<uint64_t, TPackage>& destination) {
    for (const auto& entry : source) {
        const PackageKey key{kind, entry.first};
        if (selectedKeys.find(key) != selectedKeys.end()) {
            destination.emplace(entry.first, entry.second);
        }
    }
}

template <typename TPackage>
void rewriteSelectedDisplayPackageHashes(
    std::unordered_map<uint64_t, TPackage>& packagesBySocket) {
    for (auto& [socketKey, package] : packagesBySocket) {
        (void)socketKey;
        package.packageHash = package.displayPackageHash;
    }
}

}

RuntimePackageGraph NodeGraphDisplay::selectDisplayedSubgraph(
    const NodeGraphState& graphState,
    const RuntimePackageGraph& fullGraph) const {
    std::unordered_set<PackageKey, PackageKeyHash> selectedKeys;
    selectedKeys.reserve(graphState.nodes.size());

    for (const NodeGraphNode& node : graphState.nodes) {
        if (!node.displayEnabled) {
            continue;
        }

        appendRootPackagesForNode(node, selectedKeys);
    }

    expandDependencyClosure(fullGraph, selectedKeys);
    return filterGraph(fullGraph, selectedKeys);
}

void NodeGraphDisplay::appendRootPackagesForNode(
    const NodeGraphNode& node,
    std::unordered_set<PackageKey, PackageKeyHash>& selectedKeys) const {
    for (const NodeGraphSocket& output : node.outputs) {
        const std::optional<PackageKind> kind =
            displayPackageKindForPayload(output.contract.producedPayloadType);
        if (!kind.has_value()) {
            continue;
        }

        selectedKeys.insert(PackageKey{*kind, makeSocketKey(node.id, output.id)});
    }
}

void NodeGraphDisplay::expandDependencyClosure(
    const RuntimePackageGraph& fullGraph,
    std::unordered_set<PackageKey, PackageKeyHash>& selectedKeys) const {
    std::vector<PackageKey> stack;
    stack.reserve(selectedKeys.size());
    for (const PackageKey& key : selectedKeys) {
        stack.push_back(key);
    }

    while (!stack.empty()) {
        const PackageKey key = stack.back();
        stack.pop_back();

        const PackageNode* node = fullGraph.findNode(key);
        if (!node) {
            continue;
        }

        for (const PackageDependency& dependency : node->dependencies) {
            const PackageKey dependencyKey{
                RuntimePackageGraph::packageKindFor(dependency.productType),
                dependency.outputSocketKey
            };
            if (!dependencyKey.isValid()) {
                continue;
            }

            if (selectedKeys.insert(dependencyKey).second) {
                stack.push_back(dependencyKey);
            }
        }
    }
}

RuntimePackageGraph NodeGraphDisplay::filterGraph(
    const RuntimePackageGraph& fullGraph,
    const std::unordered_set<PackageKey, PackageKeyHash>& selectedKeys) const {
    RuntimePackageGraph filteredGraph{};

    filteredGraph.nodes.reserve(fullGraph.nodes.size());
    for (const PackageNode& node : fullGraph.nodes) {
        if (selectedKeys.find(node.key) == selectedKeys.end()) {
            continue;
        }

        PackageNode filteredNode = node;
        filteredNode.packageHash = displayPackageHashForNode(fullGraph, node);
        filteredGraph.nodes.push_back(filteredNode);
    }

    copySelectedPackages(
        fullGraph.compiledPackages.packageSet.modelBySocket,
        PackageKind::Model,
        selectedKeys,
        filteredGraph.compiledPackages.packageSet.modelBySocket);
    copySelectedPackages(
        fullGraph.compiledPackages.packageSet.remeshBySocket,
        PackageKind::Remesh,
        selectedKeys,
        filteredGraph.compiledPackages.packageSet.remeshBySocket);
    rewriteSelectedDisplayPackageHashes(filteredGraph.compiledPackages.packageSet.remeshBySocket);
    copySelectedPackages(
        fullGraph.compiledPackages.packageSet.voronoiBySocket,
        PackageKind::Voronoi,
        selectedKeys,
        filteredGraph.compiledPackages.packageSet.voronoiBySocket);
    rewriteSelectedDisplayPackageHashes(filteredGraph.compiledPackages.packageSet.voronoiBySocket);
    copySelectedPackages(
        fullGraph.compiledPackages.packageSet.heatBySocket,
        PackageKind::Heat,
        selectedKeys,
        filteredGraph.compiledPackages.packageSet.heatBySocket);
    rewriteSelectedDisplayPackageHashes(filteredGraph.compiledPackages.packageSet.heatBySocket);
    copySelectedPackages(
        fullGraph.compiledPackages.packageSet.contactBySocket,
        PackageKind::Contact,
        selectedKeys,
        filteredGraph.compiledPackages.packageSet.contactBySocket);
    rewriteSelectedDisplayPackageHashes(filteredGraph.compiledPackages.packageSet.contactBySocket);

    return filteredGraph;
}
