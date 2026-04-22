#pragma once

#include <unordered_set>
#include <vector>

#include "NodeGraphTypes.hpp"
#include "runtime/RuntimeECS.hpp"

class NodeGraphDisplay {
public:
    std::unordered_set<uint64_t> computeDisplaySelectedKeys(
        const NodeGraphState& graphState,
        const ECSRegistry& registry) const;
};