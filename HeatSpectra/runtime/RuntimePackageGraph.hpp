#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RuntimePackages.hpp"

enum class PackageKind : uint8_t {
    Model,
    Remesh,
    Voronoi,
    Contact,
    Heat,
};

struct PackageKey {
    PackageKind kind = PackageKind::Model;
    uint64_t outputSocketKey = 0;

    bool isValid() const {
        return outputSocketKey != 0;
    }

    bool operator==(const PackageKey& other) const {
        return kind == other.kind &&
            outputSocketKey == other.outputSocketKey;
    }
};

struct PackageKeyHash {
    std::size_t operator()(const PackageKey& key) const noexcept {
        return (static_cast<std::size_t>(key.kind) << 56) ^
            static_cast<std::size_t>(key.outputSocketKey);
    }
};

struct PackageDependency {
    NodeProductType productType = NodeProductType::None;
    uint64_t outputSocketKey = 0;

    bool isValid() const {
        return productType != NodeProductType::None &&
            outputSocketKey != 0;
    }
};

struct PackageNode {
    PackageKey key{};
    uint64_t packageHash = 0;
    std::vector<PackageDependency> dependencies;

    bool isValid() const {
        return key.isValid() && packageHash != 0;
    }

    bool matches(const PackageNode& other) const {
        return key == other.key &&
            packageHash == other.packageHash;
    }
};

struct CompiledPackages {
    RuntimePackageSet packageSet;

    bool empty() const {
        return packageSet.modelBySocket.empty() &&
            packageSet.remeshBySocket.empty() &&
            packageSet.voronoiBySocket.empty() &&
            packageSet.contactBySocket.empty() &&
            packageSet.heatBySocket.empty();
    }
};

class RuntimePackageGraph {
public:
    CompiledPackages compiledPackages;
    std::vector<PackageNode> nodes;

    const PackageNode* findNode(const PackageKey& key) const;
    const PackageNode* findProducer(NodeProductType type, uint64_t outputSocketKey) const;

    static PackageKind packageKindFor(NodeProductType type);
};
