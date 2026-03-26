#include "NodeTransform.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphHash.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodePanelUtils.hpp"
#include "NodeModelTransform.hpp"
#include "NodePayloadRegistry.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <utility>

namespace {

glm::mat4 buildLocalTransform(const NodeGraphNode& node) {
    const float translateX = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::TranslateX, 0.0));
    const float translateY = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::TranslateY, 0.0));
    const float translateZ = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::TranslateZ, 0.0));

    const float rotateXDegrees = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::RotateXDegrees, 0.0));
    const float rotateYDegrees = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::RotateYDegrees, 0.0));
    const float rotateZDegrees = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::RotateZDegrees, 0.0));

    const float scaleX = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::ScaleX, 1.0));
    const float scaleY = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::ScaleY, 1.0));
    const float scaleZ = static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::ScaleZ, 1.0));

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(translateX, translateY, translateZ));
    transform = glm::rotate(transform, glm::radians(rotateXDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(rotateYDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(rotateZDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::scale(transform, glm::vec3(scaleX, scaleY, scaleZ));
    return transform;
}

void combineTransformParams(const NodeGraphNode& node, uint64_t& outHash) {
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::TranslateX, 0.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::TranslateY, 0.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::TranslateZ, 0.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::RotateXDegrees, 0.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::RotateYDegrees, 0.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::RotateZDegrees, 0.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::ScaleX, 1.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::ScaleY, 1.0)));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(NodePanelUtils::readFloatParam(
        node, nodegraphparams::transform::ScaleZ, 1.0)));
}

}

const char* NodeTransform::typeId() const {
    return nodegraphtypes::Transform;
}

bool NodeTransform::execute(NodeGraphKernelContext& context) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const NodeDataBlock* inputGeometryValue = nullptr;
    if (meshSocket) {
        inputGeometryValue = readInput(context.node, meshSocket->id, context.executionState);
    }
    if (inputGeometryValue && inputGeometryValue->dataType != NodeDataType::Geometry) {
        inputGeometryValue = nullptr;
    }
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = NodeDataType::None;
        outputValue.payloadHandle = {};

        if (!inputGeometryValue || !payloadRegistry || inputGeometryValue->payloadHandle.key == 0) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        const GeometryData* inputGeometry = payloadRegistry->get<GeometryData>(inputGeometryValue->payloadHandle);
        if (!inputGeometry) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        GeometryData transformedGeometry = *inputGeometry;
        transformedGeometry.localToWorld = NodeModelTransform::toMatrixArray(
            NodeModelTransform::toMat4(inputGeometry->localToWorld) * buildLocalTransform(context.node));
        bumpGeometryRevision(transformedGeometry);

        outputValue.dataType = NodeDataType::Geometry;
        const uint64_t payloadKey = makeSocketKey(
            context.node.id,
            context.node.outputs[outputIndex].id);
        outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(transformedGeometry));
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
}

bool NodeTransform::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const NodeDataBlock* inputGeometryValue = nullptr;
    if (meshSocket) {
        inputGeometryValue = readInput(context.node, meshSocket->id, context.executionState);
    }
    if (inputGeometryValue && inputGeometryValue->dataType != NodeDataType::Geometry) {
        inputGeometryValue = nullptr;
    }

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    if (!inputGeometryValue) {
        NodeGraphHash::combine(outHash, 0u);
        combineTransformParams(context.node, outHash);
        return true;
    }

    NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputGeometryValue->dataType));
    NodeGraphHash::combine(outHash, inputGeometryValue->payloadHandle.key);
    NodeGraphHash::combine(outHash, inputGeometryValue->payloadHandle.revision);
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(inputGeometryValue->payloadHandle.count));
    combineTransformParams(context.node, outHash);
    return true;
}
