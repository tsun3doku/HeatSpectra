#include "RuntimePackageGraph.hpp"

const PackageNode* RuntimePackageGraph::findNode(const PackageKey& key) const {
    if (!key.isValid()) {
        return nullptr;
    }

    for (const PackageNode& node : nodes) {
        if (node.key == key) {
            return &node;
        }
    }

    return nullptr;
}

const PackageNode* RuntimePackageGraph::findProducer(NodeProductType type, uint64_t outputSocketKey) const {
    return findNode(PackageKey{packageKindFor(type), outputSocketKey});
}

PackageKind RuntimePackageGraph::packageKindFor(NodeProductType type) {
    switch (type) {
    case NodeProductType::Model:
        return PackageKind::Model;
    case NodeProductType::Remesh:
        return PackageKind::Remesh;
    case NodeProductType::Voronoi:
        return PackageKind::Voronoi;
    case NodeProductType::Contact:
        return PackageKind::Contact;
    case NodeProductType::None:
    default:
        return PackageKind::Heat;
    }
}
