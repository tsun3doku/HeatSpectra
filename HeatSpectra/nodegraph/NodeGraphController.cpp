#include "NodeGraphController.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "scene/ModelRegistry.hpp"

#include <utility>
#include <vector>

namespace {

std::vector<uint32_t> modelNodeIds(const NodeGraphState& state) {
    std::vector<uint32_t> ids;
    ids.reserve(state.nodes.size());
    for (const NodeGraphNode& node : state.nodes) {
        if (canonicalNodeTypeId(node.typeId) == nodegraphtypes::Model && node.id.isValid()) {
            ids.push_back(node.id.value);
        }
    }
    return ids;
}

}

NodeGraphController::NodeGraphController(
    NodeGraphBridge* bridge,
    const NodeRuntimeServices& services)
    : bridge(bridge),
      runtimeServices(services),
      runtime(bridge, services) {
    plan.canExecuteHeatSolve = false;
    applyPendingChanges();
}

void NodeGraphController::applyPendingChanges() {
    if (!bridge) {
        return;
    }

    NodeGraphDelta delta{};
    if (bridge->consumeChanges(revisionSeen, delta)) {
        runtime.applyDelta(delta);
        NodeGraphDebugStore::instance().setState(runtime.state());
        plan = NodeGraphExecutionPlanner::buildPlan(runtime.state());
        if (runtimeServices.modelRegistry) {
            runtimeServices.modelRegistry->removeMissingNodeModels(modelNodeIds(runtime.state()));
        }
    }
}

void NodeGraphController::tick() {
    if (!bridge) {
        return;
    }

    NodeGraphRuntimeExecutionState execState{};
    runtime.tick(&execState);
    NodeGraphDebugStore::instance().publish(
        runtime.state().revision,
        std::move(execState.sourceSocketByInputSocket),
        std::move(execState.outputValueBySocket));
}

bool NodeGraphController::canExecuteHeatSolve() const {
    return plan.canExecuteHeatSolve;
}

const NodeGraphExecutionPlan& NodeGraphController::executionPlan() const {
    return plan;
}
