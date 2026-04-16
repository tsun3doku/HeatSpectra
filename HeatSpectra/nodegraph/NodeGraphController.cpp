#include "NodeGraphController.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugCache.hpp"
#include "NodeGraphRuntimeBridge.hpp"
#include "NodePayloadRegistry.hpp"
#include "runtime/RuntimeComputePackageController.hpp"
#include "runtime/RuntimeDisplayPackageController.hpp"
#include "runtime/RuntimePackageCompiler.hpp"

#include <algorithm>
#include <utility>

NodeGraphController::NodeGraphController(
    NodeGraphBridge* bridge,
    const NodeRuntimeServices& services)
    : bridge(bridge),
      runtimeServices(services),
      runtime(bridge, services) {
    plan.isValid = false;
    applyPendingChanges();
}

void NodeGraphController::applyPendingChanges() {
    if (!bridge) {
        return;
    }

    NodeGraphDelta delta{};
    if (bridge->consumeChanges(revisionSeen, delta)) {
        runtime.applyDelta(delta);
        if (allChangesAreLayout(delta)) {
            return;
        }
        NodeGraphDebugCache::instance().setState(runtime.state(), runtimeServices.payloadRegistry);
        plan = NodeGraphCompiler::compile(runtime.state());
    }
}

void NodeGraphController::tick() {
    if (!bridge) {
        return;
    }

    NodeGraphEvaluationState execState{};
    runtime.tick(&execState);

    if (runtimeServices.runtimeComputePackageController ||
        runtimeServices.runtimeDisplayPackageController) {
        RuntimePackageCompiler packageCompiler{};
        packageCompiler.setRuntimeBridge(runtimeServices.runtimeBridge);
        packageCompiler.setRuntimeProductRegistry(runtimeServices.runtimeProductRegistry);
        const RuntimePackageGraph fullPackageGraph = packageCompiler.buildRuntimePackageGraph(
            runtime.state(),
            execState,
            runtimeServices.payloadRegistry);

        if (runtimeServices.runtimeComputePackageController) {
            fullRuntimePackageGraph = computePackageSync.sync(
                fullRuntimePackageGraph,
                fullPackageGraph,
                *runtimeServices.runtimeComputePackageController);
        } else {
            fullRuntimePackageGraph = fullPackageGraph;
        }

        if (runtimeServices.runtimeDisplayPackageController) {
            const RuntimePackageGraph displayedPackageGraph = nodeGraphDisplay.selectDisplayedSubgraph(
                runtime.state(),
                fullPackageGraph);
            displayRuntimePackageGraph = displayPackageSync.sync(
                displayRuntimePackageGraph,
                displayedPackageGraph,
                *runtimeServices.runtimeDisplayPackageController);
        } else {
            displayRuntimePackageGraph = {};
        }
    }

    if (runtimeServices.runtimeBridge) {
        runtimeServices.runtimeBridge->clear();
        for (const auto& [socketKey, package] : fullRuntimePackageGraph.compiledPackages.packageSet.remeshBySocket) {
            if (socketKey == 0 || package.remeshHandle.key == 0) {
                continue;
            }

            ProductHandle handle{};
            handle.type = NodeProductType::Remesh;
            handle.outputSocketKey = socketKey;
            runtimeServices.runtimeBridge->setRemeshProductForPayload(package.remeshHandle, handle);
        }
    }

    NodeGraphDebugCache::instance().update(
        runtime.state().revision,
        execState.sourceSocketByInputSocket,
        execState.outputBySocket);
}

bool NodeGraphController::canExecuteHeatSolve() const {
    return plan.isValid;
}

const NodeGraphCompiled& NodeGraphController::compiledState() const {
    return plan;
}

bool NodeGraphController::allChangesAreLayout(const NodeGraphDelta& delta) {
    if (delta.changes.empty()) {
        return false;
    }

    for (const NodeGraphChange& change : delta.changes) {
        if (change.reason != NodeGraphChangeReason::Layout) {
            return false;
        }
    }

    return true;
}
