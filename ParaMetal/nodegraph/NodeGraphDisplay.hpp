#pragma once

#include "NodeGraphState.hpp"
#include "NodeGraphTypes.hpp"
#include "runtime/package/RuntimePackageManager.hpp"

#include <unordered_set>
#include <vector>

class NodePayloadRegistry;
struct NodeDataBlock;
struct NodeGraphEvaluation;

class NodeGraphDisplay {
public:
    std::unordered_set<uint64_t> computeDisplayKeys(
        const NodeGraphState& graphState,
        const NodeGraphEvaluation& evaluation,
        const RuntimePackageManager& packages,
        const NodePayloadRegistry* payloadRegistry) const;
private:
    void addDisplayKeys(
        uint64_t socketKey,
        const NodeDataBlock* block,
        const RuntimePackageManager& packages,
        const NodePayloadRegistry* payloadRegistry,
        std::unordered_set<uint64_t>& selectedKeys) const;
};
