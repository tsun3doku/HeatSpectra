#pragma once

#include <cstdint>

#include "nodegraph/NodeGraphCoreTypes.hpp"

enum class NodeProductType : uint8_t {
    None,
    Model,
    Remesh,
    Voronoi,
    Contact,
    Heat,
};

struct ProductHandle {
    NodeProductType type = NodeProductType::None;
    uint64_t outputSocketKey = 0;

    bool isValid() const {
        return type != NodeProductType::None &&
            outputSocketKey != 0;
    }

    bool operator==(const ProductHandle& other) const {
        return type == other.type &&
            outputSocketKey == other.outputSocketKey;
    }
};
