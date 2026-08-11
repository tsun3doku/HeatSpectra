#pragma once

#include <string>
#include <vector>

class NodePayloadRegistry;
struct NodeGraphCompiled;
struct NodeGraphEvaluation;
struct NodeGraphState;

namespace runtimepackage {

class ValidationPass final {
public:
    bool run(
        const NodeGraphState& graph,
        const NodeGraphCompiled& graphPlan,
        const NodeGraphEvaluation& evaluation,
        const NodePayloadRegistry& payloads,
        std::vector<std::string>& errors) const;
};

}

