#pragma once

#include "NodeGraphCoreTypes.hpp"
#include "NodeGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class NodePayloadRegistry;

struct NodeDataBlock {
    NodePayloadType dataType = NodePayloadType::None;
    NodeDataHandle payloadHandle{};
    double scalarFloatValue = 0.0;
    int64_t scalarIntValue = 0;
    bool scalarBoolValue = false;
    std::unordered_map<std::string, std::string> metadata;
    std::vector<NodeGraphNodeId> lineageNodeIds;
};

enum class EvaluatedSocketStatus : uint8_t {
    Missing,
    Value,
    Error,
};

struct EvaluatedSocketValue {
    EvaluatedSocketStatus status = EvaluatedSocketStatus::Missing;
    NodeDataBlock data{};
    std::string error;
};

struct NodeGraphEvaluationState {
    std::unordered_map<uint64_t, uint64_t> sourceSocketByInputSocket;
    std::unordered_map<uint64_t, EvaluatedSocketValue> outputBySocket;
};

void populateMetadata(NodeDataBlock& dataBlock, const NodePayloadRegistry* registry = nullptr);
void buildOutputs(const NodeGraphNode& node, const std::vector<const NodeDataBlock*>& inputs, std::vector<NodeDataBlock>& outputs);
