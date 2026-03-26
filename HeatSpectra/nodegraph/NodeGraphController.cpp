#include "NodeGraphController.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphDebugStore.hpp"
#include "NodePanelUtils.hpp"
#include "NodePayloadRegistry.hpp"
#include "contact/ContactSystemController.hpp"
#include "runtime/ContactPreviewStore.hpp"
#include "runtime/RuntimePayloadController.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace {

bool allChangesAreLayout(const NodeGraphDelta& delta) {
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

}

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
        pruneProjectedRemeshRevisions();
    }
}

void NodeGraphController::tick() {
    if (!bridge) {
        return;
    }

    NodeGraphRuntimeExecutionState execState{};
    runtime.tick(&execState);
    projectGeometryOutputs(execState);
    pruneProjectedGeometryOutputs();
    projectRemeshOutputs(execState);
    projectContactOutputs(execState);
    projectSystemOutputs(execState);
    NodeGraphDebugStore::instance().publish(
        runtime.state().revision,
        std::move(execState.sourceSocketByInputSocket),
        std::move(execState.outputValueBySocket));
}

bool NodeGraphController::canExecuteHeatSolve() const {
    return plan.isValid;
}

const NodeGraphCompiled& NodeGraphController::compiledState() const {
    return plan;
}

void NodeGraphController::projectGeometryOutputs(const NodeGraphRuntimeExecutionState& execState) {
    if (!runtimeServices.runtimePayloadController || !runtimeServices.payloadRegistry) {
        return;
    }

    for (const NodeGraphNode& node : runtime.state().nodes) {
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedDataType != NodeDataType::Geometry) {
                continue;
            }

            const uint64_t outputSocketKey = socketKey(node.id, output.id);
            const auto outputIt = execState.outputValueBySocket.find(outputSocketKey);
            if (outputIt == execState.outputValueBySocket.end()) {
                continue;
            }

            const NodeDataBlock& outputBlock = outputIt->second;
            if (outputBlock.dataType != NodeDataType::Geometry || outputBlock.payloadHandle.key == 0) {
                continue;
            }

            const auto projectedIt = projectedPayloadRevisionBySocketKey.find(outputSocketKey);
            if (projectedIt != projectedPayloadRevisionBySocketKey.end() &&
                projectedIt->second == outputBlock.payloadHandle.revision) {
                continue;
            }

            const GeometryData* geometry = runtimeServices.payloadRegistry->get<GeometryData>(outputBlock.payloadHandle);
            if (!geometry || geometry->modelId == 0) {
                continue;
            }

            const auto projectedNodeModelIt = projectedGeometryNodeModelIdBySocketKey.find(outputSocketKey);
            if (projectedNodeModelIt != projectedGeometryNodeModelIdBySocketKey.end() &&
                projectedNodeModelIt->second != 0 &&
                projectedNodeModelIt->second != geometry->modelId) {
                runtimeServices.runtimePayloadController->removeGeometryPayload(projectedNodeModelIt->second);
            }

            runtimeServices.runtimePayloadController->applyGeometryPayload(*geometry);
            projectedPayloadRevisionBySocketKey[outputSocketKey] = outputBlock.payloadHandle.revision;
            projectedPayloadTypeBySocketKey[outputSocketKey] = NodeDataType::Geometry;
            projectedGeometryNodeModelIdBySocketKey[outputSocketKey] = geometry->modelId;
        }
    }
}

void NodeGraphController::projectRemeshOutputs(const NodeGraphRuntimeExecutionState& execState) {
    if (!runtimeServices.runtimePayloadController || !runtimeServices.payloadRegistry) {
        return;
    }

    auto clearProjectedRemeshNode = [this](uint32_t nodeId) {
        const auto projectedHandleIt = projectedRemeshIntrinsicHandleByNodeId.find(nodeId);
        if (projectedHandleIt != projectedRemeshIntrinsicHandleByNodeId.end() &&
            projectedHandleIt->second.key != 0) {
            runtimeServices.runtimePayloadController->removeIntrinsicPayload(projectedHandleIt->second);
            projectedRemeshIntrinsicHandleByNodeId.erase(projectedHandleIt);
        }
        projectedRemeshRevisionByNodeId.erase(nodeId);
    };

    for (const NodeGraphNode& node : runtime.state().nodes) {
        if (getNodeTypeId(node.typeId) != nodegraphtypes::Remesh) {
            continue;
        }

        const NodeGraphSocket* geometryOutputSocket = nullptr;
        const NodeGraphSocket* intrinsicOutputSocket = nullptr;
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedDataType == NodeDataType::Geometry) {
                geometryOutputSocket = &output;
            } else if (output.contract.producedDataType == NodeDataType::Intrinsic) {
                intrinsicOutputSocket = &output;
            }
        }
        if (!geometryOutputSocket || !intrinsicOutputSocket) {
            clearProjectedRemeshNode(node.id.value);
            continue;
        }

        const auto outputIt = execState.outputValueBySocket.find(socketKey(node.id, geometryOutputSocket->id));
        if (outputIt == execState.outputValueBySocket.end()) {
            clearProjectedRemeshNode(node.id.value);
            continue;
        }

        const NodeDataBlock& outputBlock = outputIt->second;
        if (outputBlock.dataType != NodeDataType::Geometry || outputBlock.payloadHandle.key == 0) {
            clearProjectedRemeshNode(node.id.value);
            continue;
        }

        const auto intrinsicIt = execState.outputValueBySocket.find(socketKey(node.id, intrinsicOutputSocket->id));
        if (intrinsicIt == execState.outputValueBySocket.end()) {
            clearProjectedRemeshNode(node.id.value);
            continue;
        }
        const NodeDataBlock& intrinsicBlock = intrinsicIt->second;
        if (intrinsicBlock.dataType != NodeDataType::Intrinsic || intrinsicBlock.payloadHandle.key == 0) {
            clearProjectedRemeshNode(node.id.value);
            continue;
        }

        const GeometryData* geometry = runtimeServices.payloadRegistry->get<GeometryData>(outputBlock.payloadHandle);
        const IntrinsicMeshData* intrinsic = runtimeServices.payloadRegistry->get<IntrinsicMeshData>(intrinsicBlock.payloadHandle);
        if (!geometry || !intrinsic || geometry->modelId == 0) {
            clearProjectedRemeshNode(node.id.value);
            continue;
        }

        const auto projectedIt = projectedRemeshRevisionByNodeId.find(node.id.value);
        if (projectedIt != projectedRemeshRevisionByNodeId.end() &&
            projectedIt->second == outputBlock.payloadHandle.revision) {
            continue;
        }

        const auto projectedHandleIt = projectedRemeshIntrinsicHandleByNodeId.find(node.id.value);
        if (projectedHandleIt != projectedRemeshIntrinsicHandleByNodeId.end() &&
            projectedHandleIt->second.key != 0 &&
            !(projectedHandleIt->second == geometry->intrinsicHandle)) {
            runtimeServices.runtimePayloadController->removeIntrinsicPayload(projectedHandleIt->second);
        }

        runtimeServices.runtimePayloadController->applyRemeshPayload(*geometry, *intrinsic);
        projectedRemeshRevisionByNodeId[node.id.value] = outputBlock.payloadHandle.revision;
        projectedRemeshIntrinsicHandleByNodeId[node.id.value] = geometry->intrinsicHandle;
    }
}

void NodeGraphController::pruneProjectedGeometryOutputs() {
    if (!runtimeServices.runtimePayloadController) {
        return;
    }

    std::unordered_map<uint64_t, bool> activeGeometrySocketKeys;
    activeGeometrySocketKeys.reserve(runtime.state().nodes.size());
    for (const NodeGraphNode& node : runtime.state().nodes) {
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedDataType == NodeDataType::Geometry) {
                activeGeometrySocketKeys.emplace(socketKey(node.id, output.id), true);
            }
        }
    }

    std::vector<uint64_t> staleGeometrySocketKeys;
    staleGeometrySocketKeys.reserve(projectedGeometryNodeModelIdBySocketKey.size());
    for (const auto& [outputSocketKey, nodeModelId] : projectedGeometryNodeModelIdBySocketKey) {
        if (activeGeometrySocketKeys.find(outputSocketKey) != activeGeometrySocketKeys.end()) {
            continue;
        }

        if (nodeModelId != 0) {
            runtimeServices.runtimePayloadController->removeGeometryPayload(nodeModelId);
        }
        staleGeometrySocketKeys.push_back(outputSocketKey);
    }

    for (uint64_t outputSocketKey : staleGeometrySocketKeys) {
        projectedGeometryNodeModelIdBySocketKey.erase(outputSocketKey);
        projectedPayloadRevisionBySocketKey.erase(outputSocketKey);
        projectedPayloadTypeBySocketKey.erase(outputSocketKey);
    }
}

void NodeGraphController::pruneProjectedRemeshRevisions() {
    std::vector<uint32_t> activeRemeshNodeIds;
    activeRemeshNodeIds.reserve(runtime.state().nodes.size());
    for (const NodeGraphNode& node : runtime.state().nodes) {
        if (getNodeTypeId(node.typeId) == nodegraphtypes::Remesh && node.id.isValid()) {
            activeRemeshNodeIds.push_back(node.id.value);
        }
    }

    for (auto it = projectedRemeshRevisionByNodeId.begin(); it != projectedRemeshRevisionByNodeId.end();) {
        if (std::find(activeRemeshNodeIds.begin(), activeRemeshNodeIds.end(), it->first) == activeRemeshNodeIds.end()) {
            const auto handleIt = projectedRemeshIntrinsicHandleByNodeId.find(it->first);
            if (handleIt != projectedRemeshIntrinsicHandleByNodeId.end() &&
                handleIt->second.key != 0 &&
                runtimeServices.runtimePayloadController) {
                runtimeServices.runtimePayloadController->removeIntrinsicPayload(handleIt->second);
                projectedRemeshIntrinsicHandleByNodeId.erase(handleIt);
            }
            it = projectedRemeshRevisionByNodeId.erase(it);
        } else {
            ++it;
        }
    }
}

void NodeGraphController::projectContactOutputs(const NodeGraphRuntimeExecutionState& execState) {
    if (!runtimeServices.payloadRegistry ||
        !runtimeServices.contactSystemController ||
        !runtimeServices.contactPreviewStore) {
        return;
    }

    for (const NodeGraphNode& node : runtime.state().nodes) {
        if (getNodeTypeId(node.typeId) != nodegraphtypes::Contact) {
            continue;
        }

        bool updatedPreview = false;
        for (const NodeGraphSocket& output : node.outputs) {
            if (output.contract.producedDataType != NodeDataType::Contact) {
                continue;
            }

            const auto outputIt = execState.outputValueBySocket.find(socketKey(node.id, output.id));
            if (outputIt == execState.outputValueBySocket.end()) {
                continue;
            }

            const NodeDataBlock& outputBlock = outputIt->second;
            if (outputBlock.dataType != NodeDataType::Contact || outputBlock.payloadHandle.key == 0) {
                continue;
            }

            const ContactData* contact = runtimeServices.payloadRegistry->get<ContactData>(outputBlock.payloadHandle);
            if (!contact || !contact->active || contact->bindings.empty()) {
                continue;
            }

            const ContactPairData& pair = contact->bindings.front().pair;
            if (!pair.hasValidContact) {
                continue;
            }

            ContactSystem::Result previewResult{};
            ContactSystemController::HandleInfo handleInfo{};
            bool previewChanged = false;
            const bool hasPreview = runtimeServices.contactSystemController->computePreviewForPayload(
                *runtimeServices.payloadRegistry,
                pair,
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

void NodeGraphController::projectSystemOutputs(const NodeGraphRuntimeExecutionState& execState) {
    if (!runtimeServices.runtimePayloadController || !runtimeServices.payloadRegistry) {
        return;
    }

    std::unordered_map<uint64_t, NodeDataType> seenOutputTypesBySocketKey;
    seenOutputTypesBySocketKey.reserve(projectedPayloadTypeBySocketKey.size() + runtime.state().nodes.size());

    auto projectOutputsOfType = [&](NodeDataType producedType) {
        for (const NodeGraphNode& node : runtime.state().nodes) {
            const NodeTypeId canonicalTypeId = getNodeTypeId(node.typeId);
            if (canonicalTypeId != nodegraphtypes::HeatSolve &&
                canonicalTypeId != nodegraphtypes::Voronoi &&
                canonicalTypeId != nodegraphtypes::Contact) {
                continue;
            }

            for (const NodeGraphSocket& output : node.outputs) {
                if (output.contract.producedDataType != producedType) {
                    continue;
                }

                const uint64_t outputSocketKey = socketKey(node.id, output.id);
                seenOutputTypesBySocketKey[outputSocketKey] = producedType;

                const auto outputIt = execState.outputValueBySocket.find(outputSocketKey);
                if (outputIt == execState.outputValueBySocket.end()) {
                    continue;
                }

                const NodeDataBlock& outputBlock = outputIt->second;
                if (outputBlock.dataType != output.contract.producedDataType || outputBlock.payloadHandle.key == 0) {
                    continue;
                }

                const auto projectedIt = projectedPayloadRevisionBySocketKey.find(outputSocketKey);
                if (projectedIt != projectedPayloadRevisionBySocketKey.end() &&
                    projectedIt->second == outputBlock.payloadHandle.revision) {
                    continue;
                }

                if (producedType == NodeDataType::Voronoi) {
                    const VoronoiData* voronoi = runtimeServices.payloadRegistry->get<VoronoiData>(outputBlock.payloadHandle);
                    if (!voronoi) {
                        continue;
                    }
                    runtimeServices.runtimePayloadController->applyVoronoiPayload(
                        *runtimeServices.payloadRegistry,
                        *voronoi);
                } else if (producedType == NodeDataType::Contact) {
                    const ContactData* contact = runtimeServices.payloadRegistry->get<ContactData>(outputBlock.payloadHandle);
                    if (!contact) {
                        continue;
                    }
                    runtimeServices.runtimePayloadController->applyContactPayload(
                        *runtimeServices.payloadRegistry,
                        *contact);
                } else {
                    const HeatData* heat = runtimeServices.payloadRegistry->get<HeatData>(outputBlock.payloadHandle);
                    if (!heat) {
                        continue;
                    }
                    runtimeServices.runtimePayloadController->applyHeatPayload(
                        *runtimeServices.payloadRegistry,
                        *heat);

                    if (canonicalTypeId == nodegraphtypes::HeatSolve &&
                        bridge &&
                        heat->resetRequested) {
                        NodePanelUtils::writeBoolParam(
                            bridge,
                            node.id,
                            nodegraphparams::heatsolve::ResetRequested,
                            false);
                    }
                }

                projectedPayloadRevisionBySocketKey[outputSocketKey] = outputBlock.payloadHandle.revision;
                projectedPayloadTypeBySocketKey[outputSocketKey] = producedType;
            }
        }
    };

    projectOutputsOfType(NodeDataType::Voronoi);
    projectOutputsOfType(NodeDataType::Contact);
    projectOutputsOfType(NodeDataType::Heat);

    std::vector<uint64_t> staleSocketKeys;
    staleSocketKeys.reserve(projectedPayloadTypeBySocketKey.size());
    for (const auto& [outputSocketKey, projectedType] : projectedPayloadTypeBySocketKey) {
        if (projectedType != NodeDataType::Voronoi &&
            projectedType != NodeDataType::Contact &&
            projectedType != NodeDataType::Heat) {
            continue;
        }

        if (seenOutputTypesBySocketKey.find(outputSocketKey) != seenOutputTypesBySocketKey.end()) {
            continue;
        }

        if (projectedType == NodeDataType::Voronoi) {
            runtimeServices.runtimePayloadController->applyVoronoiPayload(VoronoiData{});
        } else if (projectedType == NodeDataType::Contact) {
            runtimeServices.runtimePayloadController->applyContactPayload(ContactData{});
        } else if (projectedType == NodeDataType::Heat) {
            runtimeServices.runtimePayloadController->applyHeatPayload(HeatData{});
        }
        staleSocketKeys.push_back(outputSocketKey);
    }

    for (uint64_t outputSocketKey : staleSocketKeys) {
        projectedPayloadRevisionBySocketKey.erase(outputSocketKey);
        projectedPayloadTypeBySocketKey.erase(outputSocketKey);
    }
}

uint64_t NodeGraphController::socketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}
