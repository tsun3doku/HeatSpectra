#include "ValidationPass.hpp"

#include "nodegraph/NodeGraphEvaluation.hpp"
#include "nodegraph/NodeGraphState.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

bool runtimepackage::ValidationPass::run(
    const NodeGraphState& graph,
    const NodeGraphCompiled& graphPlan,
    const NodeGraphEvaluation& evaluation,
    const NodePayloadRegistry& payloads,
    std::vector<std::string>& errors) const {
    (void)evaluation;
    (void)payloads;
    if (!graphPlan.isValid) {
        errors.push_back("Structural graph compilation failed.");
        return false;
    }
    if (!graph.nodes.empty() && graphPlan.executionOrder.size() != graph.nodes.size()) {
        errors.push_back("Package compilation received an incomplete graph schedule.");
        return false;
    }
    for (NodeGraphNodeId nodeId : graphPlan.executionOrder) {
        if (!graph.node(nodeId)) {
            errors.push_back("Package schedule references a missing node.");
            return false;
        }
    }
    return true;
}

