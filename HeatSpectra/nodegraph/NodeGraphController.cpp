#include "NodeGraphController.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "NodeGraphRuntimeBridge.hpp"
#include "NodePayloadRegistry.hpp"
#include "NodeContactParams.hpp"
#include "NodeHeatSolveParams.hpp"
#include "NodeModelParams.hpp"
#include "NodeRemeshParams.hpp"
#include "NodeVoronoiParams.hpp"
#include "NodePanelUtils.hpp"
#include "contact/ContactSystemController.hpp"
#include "runtime/RenderSettingsController.hpp"
#include "runtime/RuntimePackageCompiler.hpp"
#include "runtime/RuntimePackageController.hpp"

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

    updateNodeOwnedRenderSettings();
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

void NodeGraphController::updateNodeOwnedRenderSettings() {
    if (!runtimeServices.renderSettingsController) {
        return;
    }

    bool showWireframe = false;
    bool showRemeshOverlay = false;
    bool showFaceNormals = false;
    bool showVertexNormals = false;
    bool showHeatOverlay = false;
    bool showVoronoi = false;
    bool showPoints = false;
    bool showContactLines = false;
    float normalLength = 0.05f;
    bool hasNormalLength = false;

    for (const NodeGraphNode& node : runtime.state().nodes) {
        const NodeTypeId typeId = getNodeTypeId(node.typeId);
        if (typeId == nodegraphtypes::Model) {
            const ModelNodeParams params = readModelNodeParams(node);
            showWireframe =
                showWireframe ||
                params.preview.showWireframe;
            continue;
        }

        if (typeId == nodegraphtypes::Remesh) {
            const RemeshNodeParams params = readRemeshNodeParams(node);
            const bool remeshOverlay = params.preview.showRemeshOverlay;
            const bool faceNormals = params.preview.showFaceNormals;
            const bool vertexNormals = params.preview.showVertexNormals;
            if (remeshOverlay || faceNormals || vertexNormals) {
                const float nodeNormalLength = static_cast<float>(params.normalLength);
                if (!hasNormalLength) {
                    normalLength = nodeNormalLength;
                    hasNormalLength = true;
                } else {
                    normalLength = std::max(normalLength, nodeNormalLength);
                }
            }

            showRemeshOverlay = showRemeshOverlay || remeshOverlay;
            showFaceNormals = showFaceNormals || faceNormals;
            showVertexNormals = showVertexNormals || vertexNormals;
            continue;
        }

        if (typeId == nodegraphtypes::HeatSolve) {
            const HeatSolveNodeParams params = readHeatSolveNodeParams(node);
            showHeatOverlay =
                showHeatOverlay ||
                params.preview.showHeatOverlay;
            continue;
        }

        if (typeId == nodegraphtypes::Voronoi) {
            const VoronoiNodeParams params = readVoronoiNodeParams(node);
            showVoronoi =
                showVoronoi ||
                params.preview.showVoronoi;
            showPoints =
                showPoints ||
                params.preview.showPoints;
            continue;
        }

        if (typeId == nodegraphtypes::Contact) {
            const ContactNodeParams params = readContactNodeParams(node);
            showContactLines =
                showContactLines ||
                params.preview.showContactLines;
            continue;
        }
    }

    runtimeServices.renderSettingsController->setWireframeMode(
        showWireframe ? RenderSettingsController::WireframeMode::Wireframe
                      : RenderSettingsController::WireframeMode::Off);
    runtimeServices.renderSettingsController->setIntrinsicOverlayEnabled(showRemeshOverlay);
    runtimeServices.renderSettingsController->setIntrinsicNormalsEnabled(showFaceNormals);
    runtimeServices.renderSettingsController->setIntrinsicVertexNormalsEnabled(showVertexNormals);
    runtimeServices.renderSettingsController->setIntrinsicNormalLength(normalLength);
    runtimeServices.renderSettingsController->setHeatOverlayEnabled(showHeatOverlay);
    runtimeServices.renderSettingsController->setVoronoiEnabled(showVoronoi);
    runtimeServices.renderSettingsController->setPointsEnabled(showPoints);
    runtimeServices.renderSettingsController->setContactLinesEnabled(showContactLines);
    runtimeServices.renderSettingsController->setSurfelsEnabled(false);
}

void NodeGraphController::updateContactPreviews(const NodeGraphEvaluationState& execState) {
    if (!runtimeServices.contactSystemController) {
        return;
    }

    std::unordered_map<uint64_t, OutputPreviewState> nextPreviewStateBySocket;
    nextPreviewStateBySocket.reserve(previewStateBySocket.size() + runtimePackages.contactBySocket.size());

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
            const ContactNodeParams params = readContactNodeParams(node);
            const bool authoredPreviewEnabled = params.preview.showContactLines;
            const bool previewEnabled =
                authoredPreviewEnabled &&
                contactPackage.authored.active &&
                contactPackage.authored.pair.hasValidContact;
            if (!previewEnabled) {
                runtimeServices.contactSystemController->setPreviewEnabled(outputSocketKey, false);
                previewStateBySocket.erase(outputSocketKey);
                updatedPreview = true;
                continue;
            }

            OutputPreviewState nextState{};
            nextState.enabled = true;

            const auto previousIt = previewStateBySocket.find(outputSocketKey);
            const bool previewStateUnchanged =
                previousIt != previewStateBySocket.end() &&
                previousIt->second.enabled == nextState.enabled;
            if (previewStateUnchanged) {
                nextPreviewStateBySocket[outputSocketKey] = nextState;
                updatedPreview = true;
                break;
            }

            runtimeServices.contactSystemController->setPreviewEnabled(outputSocketKey, true);
            nextPreviewStateBySocket[outputSocketKey] = nextState;
            updatedPreview = true;
            break;
        }

        if (!updatedPreview) {
            for (const NodeGraphSocket& output : node.outputs) {
                if (output.contract.producedPayloadType == NodePayloadType::Contact) {
                    const uint64_t outputSocketKey = makeSocketKey(node.id, output.id);
                    runtimeServices.contactSystemController->setPreviewEnabled(outputSocketKey, false);
                    previewStateBySocket.erase(outputSocketKey);
                }
            }
        }
    }

    previewStateBySocket = std::move(nextPreviewStateBySocket);
}
