#include "NodeRemesh.hpp"

#include "mesh/MeshModifiers.hpp"
#include "scene/ModelRegistry.hpp"
#include "NodeGraphBridge.hpp"
#include "NodeGraphExecutionPlanner.hpp"
#include "vulkan/ResourceManager.hpp"
#include "scene/SceneController.hpp"

#include <iostream>
#include <unordered_set>
#include <vector>

const char* NodeRemesh::typeId() const {
    return nodegraphtypes::Remesh;
}

bool NodeRemesh::execute(NodeGraphKernelContext& context) const {
    NodeGraphBridge& bridge = context.executionState.bridge;
    const NodeRuntimeServices& services = context.executionState.services;
    ModelRegistry* const modelRegistry = services.modelRegistry;
    SceneController* const sceneController = services.sceneController;

    const NodeDataBlock* upstreamGeometryValue = nullptr;
    std::vector<uint32_t> targetGraphModelIds;
    std::unordered_set<uint32_t> seenGraphModelIds;
    for (const NodeDataBlock* inputValue : context.inputs) {
        if (!inputValue || inputValue->dataType != NodeDataType::Geometry) {
            continue;
        }

        if (!upstreamGeometryValue) {
            upstreamGeometryValue = inputValue;
        }
        const uint32_t graphModelId = inputValue->geometry.modelId;
        if (graphModelId != 0 && seenGraphModelIds.insert(graphModelId).second) {
            targetGraphModelIds.push_back(graphModelId);
        }
    }

    const bool runRequested = getBoolParamValue(context.node, nodegraphparams::remesh::RunRequested, false);
    bool executed = false;
    if (runRequested) {
        if (!NodeGraphExecutionPlanner::nodeHasAllRequiredInputs(context.executionState.state, context.node.id)) {
            return false;
        }

        const int iterations = getIntParamValue(context.node, nodegraphparams::remesh::Iterations, 1);
        const double minAngle = getFloatParamValue(context.node, nodegraphparams::remesh::MinAngleDegrees, 30.0);
        const double maxEdge = getFloatParamValue(context.node, nodegraphparams::remesh::MaxEdgeLength, 0.1);
        const double step = getFloatParamValue(context.node, nodegraphparams::remesh::StepSize, 0.25);

        for (uint32_t graphModelId : targetGraphModelIds) {
            uint32_t runtimeModelId = 0;
            bool remeshed = false;
            if (modelRegistry &&
                modelRegistry->tryGetNodeModelRuntimeId(graphModelId, runtimeModelId) &&
                runtimeModelId != 0 &&
                sceneController) {
                sceneController->performRemeshing(iterations, minAngle, maxEdge, step, runtimeModelId);
                remeshed = true;
            }

            if (!remeshed) {
                const NodeDataBlock* matchingInputValue = nullptr;
                for (const NodeDataBlock* inputValue : context.inputs) {
                    if (!inputValue || inputValue->dataType != NodeDataType::Geometry) {
                        continue;
                    }
                    if (inputValue->geometry.modelId == graphModelId) {
                        matchingInputValue = inputValue;
                        break;
                    }
                }

                if (modelRegistry && sceneController &&
                    matchingInputValue && !matchingInputValue->geometry.sourceModelPath.empty()) {
                    runtimeModelId = modelRegistry->getOrLoadModelID(
                        graphModelId,
                        matchingInputValue->geometry.sourceModelPath);
                    if (runtimeModelId != 0) {
                        sceneController->performRemeshing(iterations, minAngle, maxEdge, step, runtimeModelId);
                        remeshed = true;
                    }
                }
            }

            if (remeshed) {
                executed = true;
            } else {
                std::cerr << "[NodeRemesh] Failed to remesh graph model ID " << graphModelId
                          << " (no runtime model binding and no resolvable model path)." << std::endl;
            }
        }

        setBoolParameter(bridge, context.node.id, nodegraphparams::remesh::RunRequested, false);
    }

    GeometryData remeshedGeometry{};
    const bool hasRemeshedGeometry = tryBuildRemeshedGeometry(services, upstreamGeometryValue, remeshedGeometry);

    for (NodeDataBlock& outputValue : context.outputs) {
        outputValue.dataType = NodeDataType::Geometry;
        outputValue.geometry = hasRemeshedGeometry
            ? remeshedGeometry
            : (upstreamGeometryValue ? upstreamGeometryValue->geometry : GeometryData{});
        refreshNodeDataBlockMetadata(outputValue);
    }

    return executed;
}

bool NodeRemesh::getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue) {
    bool value = defaultValue;
    if (tryGetNodeParamBool(node, parameterId, value)) {
        return value;
    }

    return defaultValue;
}

int NodeRemesh::getIntParamValue(const NodeGraphNode& node, uint32_t parameterId, int defaultValue) {
    int64_t value = defaultValue;
    if (tryGetNodeParamInt(node, parameterId, value)) {
        return static_cast<int>(value);
    }

    return defaultValue;
}

double NodeRemesh::getFloatParamValue(const NodeGraphNode& node, uint32_t parameterId, double defaultValue) {
    double value = defaultValue;
    if (tryGetNodeParamFloat(node, parameterId, value)) {
        return value;
    }

    return defaultValue;
}

bool NodeRemesh::setBoolParameter(NodeGraphBridge& bridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value) {
    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Bool;
    parameter.boolValue = value;
    return bridge.setNodeParameter(nodeId, parameter);
}

bool NodeRemesh::tryGetRemeshedModelGeometry(
    const NodeRuntimeServices& services,
    uint32_t targetRuntimeModelId,
    std::vector<float>& outPointPositions,
    std::vector<uint32_t>& outTriangleIndices) {
    outPointPositions.clear();
    outTriangleIndices.clear();

    if (targetRuntimeModelId == 0 || !services.resourceManager || !services.meshModifiers) {
        return false;
    }

    Model* targetModel = services.resourceManager->getModelByID(targetRuntimeModelId);
    if (!targetModel) {
        return false;
    }

    const iODT* remesher = services.meshModifiers->getRemesher().getRemesherForModel(targetModel);
    if (!remesher) {
        return false;
    }

    const SupportingHalfedge* supportingHalfedge = remesher->getSupportingHalfedge();
    if (!supportingHalfedge) {
        return false;
    }

    const SupportingHalfedge::IntrinsicMesh intrinsicMesh = supportingHalfedge->buildIntrinsicMesh();
    if (intrinsicMesh.vertices.empty() || intrinsicMesh.indices.empty()) {
        return false;
    }

    outPointPositions.reserve(intrinsicMesh.vertices.size() * 3);
    for (const SupportingHalfedge::IntrinsicVertex& vertex : intrinsicMesh.vertices) {
        outPointPositions.push_back(vertex.position.x);
        outPointPositions.push_back(vertex.position.y);
        outPointPositions.push_back(vertex.position.z);
    }

    outTriangleIndices = intrinsicMesh.indices;
    return true;
}

bool NodeRemesh::tryBuildRemeshedGeometry(
    const NodeRuntimeServices& services,
    const NodeDataBlock* upstreamGeometryValue,
    GeometryData& outGeometry) {
    outGeometry = {};
    if (!upstreamGeometryValue || upstreamGeometryValue->geometry.modelId == 0) {
        return false;
    }

    return tryBuildRemeshedGeometryForModel(
        services,
        upstreamGeometryValue->geometry.modelId,
        upstreamGeometryValue,
        outGeometry);
}

bool NodeRemesh::tryBuildRemeshedGeometryForModel(
    const NodeRuntimeServices& services,
    uint32_t targetGraphModelId,
    const NodeDataBlock* upstreamGeometryValue,
    GeometryData& outGeometry) {
    if (!services.modelRegistry) {
        return false;
    }

    uint32_t runtimeModelId = 0;
    if (!services.modelRegistry->tryGetNodeModelRuntimeId(targetGraphModelId, runtimeModelId)) {
        return false;
    }

    std::vector<float> pointPositions;
    std::vector<uint32_t> triangleIndices;
    if (!tryGetRemeshedModelGeometry(services, runtimeModelId, pointPositions, triangleIndices)) {
        return false;
    }

    outGeometry = {};
    outGeometry.modelId = targetGraphModelId;
    if (upstreamGeometryValue) {
        outGeometry.sourceModelPath = upstreamGeometryValue->geometry.sourceModelPath;
    }
    outGeometry.pointPositions = std::move(pointPositions);
    outGeometry.triangleIndices = std::move(triangleIndices);

    GeometryAttribute positionAttribute{};
    positionAttribute.name = "P";
    positionAttribute.domain = GeometryAttributeDomain::Point;
    positionAttribute.dataType = GeometryAttributeDataType::Float;
    positionAttribute.tupleSize = 3;
    positionAttribute.floatValues = outGeometry.pointPositions;
    outGeometry.attributes.push_back(std::move(positionAttribute));
    return true;
}
