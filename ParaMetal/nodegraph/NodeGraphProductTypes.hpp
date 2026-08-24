#pragma once

#include <cstdint>

#include "hash/HashValues.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"

enum class NodeProductType : uint8_t {
    None,
    Model,
    Remesh,
    Voronoi,
    Point,
    Heat,
};

struct ProductHandle {
    NodeProductType type = NodeProductType::None;
    uint64_t outputSocketKey = 0;
    HashValues hashes{};

    bool isValid() const {
        return type != NodeProductType::None &&
            outputSocketKey != 0 &&
            hashes.full != 0;
    }

    bool operator==(const ProductHandle& other) const {
        return type == other.type &&
            outputSocketKey == other.outputSocketKey &&
            hashes.full == other.hashes.full &&
            hashes.geometry == other.hashes.geometry &&
            hashes.thermal == other.hashes.thermal &&
            hashes.simulation == other.hashes.simulation &&
            hashes.display == other.hashes.display;
    }
};
