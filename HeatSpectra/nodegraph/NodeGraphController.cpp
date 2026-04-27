#include "NodeGraphController.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugCache.hpp"
#include "NodeGraphRuntimeBridge.hpp"
#include "NodePayloadRegistry.hpp"
#include "runtime/RuntimeContactComputeTransport.hpp"
#include "runtime/RuntimeContactDisplayTransport.hpp"
#include "runtime/RuntimeHeatComputeTransport.hpp"
#include "runtime/RuntimeHeatDisplayTransport.hpp"
#include "runtime/RuntimeModelComputeTransport.hpp"
#include "runtime/RuntimeModelDisplayTransport.hpp"
#include "runtime/RuntimePackageCompiler.hpp"
#include "runtime/RuntimeRemeshComputeTransport.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeVoronoiComputeTransport.hpp"
#include "runtime/RuntimeVoronoiDisplayTransport.hpp"
#include "runtime/RuntimeECS.hpp"

NodeGraphController::NodeGraphController(NodeGraphBridge* bridge, const NodeRuntimeServices& services)
    : bridge(bridge),
      runtimeServices(services),
      runtime(bridge, services) {
    plan.isValid = false;
    if (runtimeServices.modelComputeTransport) { runtimeServices.modelComputeTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.remeshComputeTransport) { runtimeServices.remeshComputeTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.voronoiComputeTransport) { runtimeServices.voronoiComputeTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.contactComputeTransport) { runtimeServices.contactComputeTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.heatComputeTransport) { runtimeServices.heatComputeTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.remeshDisplayTransport) { runtimeServices.remeshDisplayTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.voronoiDisplayTransport) { runtimeServices.voronoiDisplayTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.contactDisplayTransport) { runtimeServices.contactDisplayTransport->setECSRegistry(&ecsRegistry); }
    if (runtimeServices.heatDisplayTransport) { runtimeServices.heatDisplayTransport->setECSRegistry(&ecsRegistry); }
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
    runtime.tick(&execState, plan);

    const bool hasComputeTransports = runtimeServices.modelComputeTransport ||
        runtimeServices.remeshComputeTransport ||
        runtimeServices.voronoiComputeTransport ||
        runtimeServices.contactComputeTransport ||
        runtimeServices.heatComputeTransport;
    const bool hasDisplayTransports = runtimeServices.modelDisplayTransport ||
        runtimeServices.remeshDisplayTransport ||
        runtimeServices.voronoiDisplayTransport ||
        runtimeServices.contactDisplayTransport ||
        runtimeServices.heatDisplayTransport;

    if (hasComputeTransports || hasDisplayTransports) {
        RuntimePackageCompiler packageCompiler{};
        packageCompiler.setRuntimeBridge(runtimeServices.runtimeBridge);

        // Snapshot all package entities as stale before compilation
        std::unordered_set<ECSEntity> staleEntities = collectPackageEntities(ecsRegistry);

        // Compile and apply packages directly to registry 
        packageCompiler.compileAndApply(runtime.state(), execState, runtimeServices.payloadRegistry, ecsRegistry, staleEntities);

        destroyStaleEntities(ecsRegistry, staleEntities);

        // Sync compute transports 
        if (runtimeServices.modelComputeTransport) {
            runtimeServices.modelComputeTransport->sync(ecsRegistry);
            runtimeServices.modelComputeTransport->finalizeSync();
        }
        if (runtimeServices.remeshComputeTransport) {
            runtimeServices.remeshComputeTransport->sync(ecsRegistry);
            runtimeServices.remeshComputeTransport->finalizeSync();
        }
        if (runtimeServices.voronoiComputeTransport) {
            runtimeServices.voronoiComputeTransport->sync(ecsRegistry);
            runtimeServices.voronoiComputeTransport->finalizeSync();
        }
        if (runtimeServices.contactComputeTransport) {
            runtimeServices.contactComputeTransport->sync(ecsRegistry);
            runtimeServices.contactComputeTransport->finalizeSync();
        }
        if (runtimeServices.heatComputeTransport) {
            runtimeServices.heatComputeTransport->sync(ecsRegistry);
            runtimeServices.heatComputeTransport->finalizeSync();
        }

        if (hasDisplayTransports) {
            const std::unordered_set<uint64_t> visibleKeys =
                nodeGraphDisplay.computeDisplaySelectedKeys(
                    runtime.state(),
                    ecsRegistry);

            if (runtimeServices.modelDisplayTransport) { runtimeServices.modelDisplayTransport->setVisibleKeys(&visibleKeys); }
            if (runtimeServices.remeshDisplayTransport) { runtimeServices.remeshDisplayTransport->setVisibleKeys(&visibleKeys); }
            if (runtimeServices.voronoiDisplayTransport) { runtimeServices.voronoiDisplayTransport->setVisibleKeys(&visibleKeys); }
            if (runtimeServices.contactDisplayTransport) { runtimeServices.contactDisplayTransport->setVisibleKeys(&visibleKeys); }
            if (runtimeServices.heatDisplayTransport) { runtimeServices.heatDisplayTransport->setVisibleKeys(&visibleKeys); }

            if (runtimeServices.modelDisplayTransport) {
                runtimeServices.modelDisplayTransport->sync(ecsRegistry);
                runtimeServices.modelDisplayTransport->finalizeSync();
            }
            if (runtimeServices.remeshDisplayTransport) {
                runtimeServices.remeshDisplayTransport->sync(ecsRegistry);
                runtimeServices.remeshDisplayTransport->finalizeSync();
            }
            if (runtimeServices.voronoiDisplayTransport) {
                runtimeServices.voronoiDisplayTransport->sync(ecsRegistry);
                runtimeServices.voronoiDisplayTransport->finalizeSync();
            }
            if (runtimeServices.contactDisplayTransport) {
                runtimeServices.contactDisplayTransport->sync(ecsRegistry);
                runtimeServices.contactDisplayTransport->finalizeSync();
            }
            if (runtimeServices.heatDisplayTransport) {
                runtimeServices.heatDisplayTransport->sync(ecsRegistry);
                runtimeServices.heatDisplayTransport->finalizeSync();
            }
        }

        if (runtimeServices.runtimeBridge) {
            runtimeServices.runtimeBridge->clear();
            for (auto entity : ecsRegistry.view<RemeshPackage>()) {
                uint64_t socketKey = static_cast<uint64_t>(entity);
                const auto& package = ecsRegistry.get<RemeshPackage>(entity);
                if (socketKey == 0 || package.remeshHandle.key == 0) {
                    continue;
                }

                ProductHandle handle{};
                handle.type = NodeProductType::Remesh;
                handle.outputSocketKey = socketKey;
                runtimeServices.runtimeBridge->setRemeshProductForPayload(package.remeshHandle, handle);
            }
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
