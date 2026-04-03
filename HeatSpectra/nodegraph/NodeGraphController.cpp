#include "NodeGraphController.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "NodeGraphRuntimeBridge.hpp"
#include "NodePayloadRegistry.hpp"
#include "contact/ContactSystemController.hpp"
#include "runtime/ContactPreviewStore.hpp"
#include "runtime/RuntimePackageCompiler.hpp"
#include "runtime/RuntimePackageController.hpp"
#include "runtime/RuntimeContactTransport.hpp"

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
        NodeGraphDebugStore::instance().setState(runtime.state(), runtimeServices.payloadRegistry);
        plan = NodeGraphCompiler::compile(runtime.state());
    }
}

void NodeGraphController::tick() {
    if (!bridge) {
        return;
    }

    NodeGraphEvaluationState execState{};
    runtime.tick(&execState);

    if (runtimeServices.runtimePackageController) {
        RuntimePackageCompiler packageCompiler{};
        packageCompiler.setSceneController(runtimeServices.sceneController);
        packageCompiler.setRuntimeBridge(runtimeServices.runtimeBridge);
        packageCompiler.setRuntimeProductRegistry(runtimeServices.runtimeProductRegistry);
        RuntimePackageSet nextRuntimePackages = packageCompiler.buildRuntimePackageSet(
            runtime.state(),
            execState,
            runtimeServices.payloadRegistry);
        runtimePackageSync.sync(runtimePackages, nextRuntimePackages, *runtimeServices.runtimePackageController);
        runtimePackages = std::move(nextRuntimePackages);
    }

    if (runtimeServices.runtimeBridge) {
        runtimeServices.runtimeBridge->clear();
        for (const auto& [socketKey, package] : runtimePackages.remeshBySocket) {
            if (socketKey == 0 || package.remeshHandle.key == 0) {
                continue;
            }

            ProductHandle handle{};
            handle.type = NodeProductType::Remesh;
            handle.outputSocketKey = socketKey;
            runtimeServices.runtimeBridge->setRemeshProductForPayload(package.remeshHandle, handle);
        }
    }

    updateContactPreviews(execState);
    NodeGraphDebugStore::instance().publish(
        runtime.state().revision,
        std::move(execState.sourceSocketByInputSocket),
        std::move(execState.outputBySocket));
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

void NodeGraphController::updateContactPreviews(const NodeGraphEvaluationState& execState) {
    if (!runtimeServices.contactSystemController ||
        !runtimeServices.contactPreviewStore) {
        return;
    }

    for (const NodeGraphNode& node : runtime.state().nodes) {
        if (getNodeTypeId(node.typeId) != nodegraphtypes::Contact) {
            continue;
        }

        bool updatedPreview = false;
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedPayloadType != NodePayloadType::Contact) {
                continue;
            }

            const auto outputIt = execState.outputBySocket.find(makeSocketKey(node.id, output.id));
            if (outputIt == execState.outputBySocket.end() ||
                outputIt->second.status != EvaluatedSocketStatus::Value) {
                continue;
            }

            const NodeDataBlock& outputBlock = outputIt->second.data;
            if (outputBlock.dataType != NodePayloadType::Contact || outputBlock.payloadHandle.key == 0) {
                continue;
            }

            const uint64_t outputSocketKey = makeSocketKey(node.id, output.id);
            const auto packageIt = runtimePackages.contactBySocket.find(outputSocketKey);
            if (packageIt == runtimePackages.contactBySocket.end()) {
                continue;
            }

            const ContactPackage& contactPackage = packageIt->second;
            if (!contactPackage.authored.active ||
                !contactPackage.authored.pair.hasValidContact) {
                continue;
            }

            RuntimeContactBinding previewBinding{};
            if (!RuntimeContactTransport::buildBinding(
                    contactPackage,
                    runtimeServices.runtimeProductRegistry,
                    previewBinding)) {
                runtimeServices.contactPreviewStore->clearPreviewForNode(node.id.value);
                updatedPreview = true;
                break;
            }

            ContactSystem::Result previewResult{};
            ContactSystemController::HandleInfo handleInfo{};
            bool previewChanged = false;
            const bool hasPreview = runtimeServices.contactSystemController->computePreviewForRuntimePair(
                previewBinding.runtimePair,
                previewResult,
                handleInfo,
                false,
                previewChanged);
            if (hasPreview && previewChanged) {
                runtimeServices.contactPreviewStore->setPreviewForNode(node.id.value, previewResult);
            } else if (!hasPreview) {
                runtimeServices.contactPreviewStore->clearPreviewForNode(node.id.value);
            }

            updatedPreview = true;
            break;
        }

        if (!updatedPreview) {
            runtimeServices.contactPreviewStore->clearPreviewForNode(node.id.value);
        }
    }
}
