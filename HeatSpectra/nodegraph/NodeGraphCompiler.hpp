#pragma once

#include "NodeGraphTypes.hpp"

#include <string>
#include <vector>
#include <unordered_set>

struct NodeGraphCompiled {
    uint64_t revision = 0;
    bool isValid = false;
    std::vector<NodeGraphNodeId> executionOrder;
    std::vector<std::string> compilationErrors;
};

class NodeGraphCompiler {
public:
    static NodeGraphCompiled compile(const NodeGraphState& state);

private:
    static bool nodeHasAllRequiredInputs(const NodeGraphState& state, NodeGraphNodeId nodeId);
    static bool buildTopologicalOrder(const NodeGraphState& state, std::vector<NodeGraphNodeId>& outOrder, std::string* outError = nullptr);

    static std::unordered_set<uint64_t> buildConnectedInputSocketSet(const NodeGraphState& state);
    static std::vector<std::string> findMissingInputSocketNames(const NodeGraphNode& node, const std::unordered_set<uint64_t>& connectedInputSockets);
    static std::string formatMissingInputsText(const std::vector<std::string>& socketNames);
    static std::string makeInvariantReason(const char* code, const std::string& detail);
};
