#include "NodeRemesh.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphHash.hpp"
#include "NodePanelUtils.hpp"
#include "domain/RemeshParams.hpp"
#include "mesh/remesher/Remesher.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"

namespace {

NodeDataHandle makeHandleForSocket(const NodeGraphNode& node, NodeGraphSocketId socketId) {
    NodeDataHandle handle{};
    handle.key = makeSocketKey(node.id, socketId);
    return handle;
}

}

const char* NodeRemesh::typeId() const {
    return nodegraphtypes::Remesh;
}

bool NodeRemesh::execute(NodeGraphKernelContext& context) const {
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
    Remesher* const remesher = context.executionState.services.remesher;
    std::vector<NodeDataHandle> outputHandles(context.node.outputs.size());

    const NodeGraphSocket* meshInputSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const NodeDataBlock* upstreamGeometryValue = nullptr;
    if (meshInputSocket) {
        upstreamGeometryValue = readInput(context.node, meshInputSocket->id, context.executionState);
    }
    if (upstreamGeometryValue && upstreamGeometryValue->dataType != NodeDataType::Geometry) {
        upstreamGeometryValue = nullptr;
    }

    const GeometryData* upstreamGeometry =
        (payloadRegistry && upstreamGeometryValue)
        ? payloadRegistry->get<GeometryData>(upstreamGeometryValue->payloadHandle)
        : nullptr;

    bool executed = false;
    if (upstreamGeometry && payloadRegistry && remesher) {
        const RemeshParams defaults{};
        RemeshResultData result{};
        const int iterations = NodePanelUtils::readIntParam(
            context.node,
            nodegraphparams::remesh::Iterations,
            defaults.iterations);
        const double minAngle = NodePanelUtils::readFloatParam(
            context.node,
            nodegraphparams::remesh::MinAngleDegrees,
            defaults.minAngleDegrees);
        const double maxEdge = NodePanelUtils::readFloatParam(
            context.node,
            nodegraphparams::remesh::MaxEdgeLength,
            defaults.maxEdgeLength);
        const double step = NodePanelUtils::readFloatParam(
            context.node,
            nodegraphparams::remesh::StepSize,
            defaults.stepSize);
        if (remesher->remesh(*upstreamGeometry, iterations, minAngle, maxEdge, step, result)) {
            NodeDataHandle intrinsicHandle{};
            for (std::size_t outputIndex = 0; outputIndex < context.node.outputs.size(); ++outputIndex) {
                const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
                if (outputSocket.contract.producedDataType != NodeDataType::Intrinsic) {
                    continue;
                }

                const uint64_t payloadKey = makeSocketKey(context.node.id, outputSocket.id);
                intrinsicHandle = payloadRegistry->upsert(payloadKey, result.intrinsic);
                outputHandles[outputIndex] = intrinsicHandle;
            }

            result.geometry.intrinsicHandle = intrinsicHandle;
            for (std::size_t outputIndex = 0; outputIndex < context.node.outputs.size(); ++outputIndex) {
                const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
                if (outputSocket.contract.producedDataType == NodeDataType::Intrinsic) {
                    continue;
                }
                const uint64_t payloadKey = makeSocketKey(context.node.id, outputSocket.id);
                outputHandles[outputIndex] = payloadRegistry->upsert(payloadKey, result.geometry);
            }
            executed = true;
        }
    }

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = context.node.outputs[outputIndex];
        outputValue.dataType = outputSocket.contract.producedDataType;
        outputValue.payloadHandle = {};

        if (payloadRegistry) {
            NodeDataHandle outputHandle = outputHandles[outputIndex];
            if (outputHandle.key == 0) {
                outputHandle = makeHandleForSocket(context.node, outputSocket.id);
            }
            if (outputSocket.contract.producedDataType == NodeDataType::Geometry) {
                const GeometryData* geometry = payloadRegistry->get<GeometryData>(outputHandle);
                if (geometry) {
                    outputValue.payloadHandle = outputHandle;
                } else if (upstreamGeometryValue) {
                    outputValue.payloadHandle = upstreamGeometryValue->payloadHandle;
                }
            } else if (outputSocket.contract.producedDataType == NodeDataType::Intrinsic) {
                const IntrinsicMeshData* intrinsic = payloadRegistry->get<IntrinsicMeshData>(outputHandle);
                if (intrinsic) {
                    outputValue.payloadHandle = outputHandle;
                }
            }
        }

        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return executed;
}

bool NodeRemesh::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const RemeshParams defaults{};
    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(NodePanelUtils::readIntParam(
        context.node, nodegraphparams::remesh::Iterations, defaults.iterations)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::remesh::MinAngleDegrees, defaults.minAngleDegrees)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::remesh::MaxEdgeLength, defaults.maxEdgeLength)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        context.node, nodegraphparams::remesh::StepSize, defaults.stepSize)));

    const NodeGraphSocket* meshInputSocket = findInputSocket(context.node, NodeGraphValueType::Mesh);
    const NodeDataBlock* inputValue = nullptr;
    if (meshInputSocket) {
        inputValue = readInput(context.node, meshInputSocket->id, context.executionState);
    }
    if (!inputValue) {
        NodeGraphHash::combine(outHash, 0);
    } else {
        NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputValue->dataType));
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.key);
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.revision);
        NodeGraphHash::combine(outHash, inputValue->payloadHandle.count);
    }
    return true;
}
