#include "NodeTransform.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphHash.hpp"
#include "NodeGraphDataTypes.hpp"
#include "NodeModelTransform.hpp"
#include "NodePayloadRegistry.hpp"
#include "NodeTransformParams.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <utility>

namespace {

glm::mat4 buildLocalTransform(const NodeGraphNode& node) {
    const TransformNodeParams params = readTransformNodeParams(node);
    const float translateX = static_cast<float>(params.translateX);
    const float translateY = static_cast<float>(params.translateY);
    const float translateZ = static_cast<float>(params.translateZ);
    const float rotateXDegrees = static_cast<float>(params.rotateXDegrees);
    const float rotateYDegrees = static_cast<float>(params.rotateYDegrees);
    const float rotateZDegrees = static_cast<float>(params.rotateZDegrees);
    const float scaleX = static_cast<float>(params.scaleX);
    const float scaleY = static_cast<float>(params.scaleY);
    const float scaleZ = static_cast<float>(params.scaleZ);

    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(translateX, translateY, translateZ));
    transform = glm::rotate(transform, glm::radians(rotateXDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(rotateYDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    transform = glm::rotate(transform, glm::radians(rotateZDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    transform = glm::scale(transform, glm::vec3(scaleX, scaleY, scaleZ));
    return transform;
}

void combineTransformParams(const NodeGraphNode& node, uint64_t& outHash) {
    const TransformNodeParams params = readTransformNodeParams(node);
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.translateX));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.translateY));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.translateZ));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.rotateXDegrees));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.rotateYDegrees));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.rotateZDegrees));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.scaleX));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.scaleY));
    NodeGraphHash::combineFloat(outHash, static_cast<float>(params.scaleZ));
}

std::array<float, 16> buildLocalTransformArray(const NodeGraphNode& node) {
    return NodeModelTransform::toMatrixArray(buildLocalTransform(node));
}

}

const char* NodeTransform::typeId() const {
    return nodegraphtypes::Transform;
}

void NodeTransform::execute(NodeGraphKernelContext& context) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);
    if (inputMeshValue && valueTypeOf(inputMeshValue->dataType) != NodeGraphValueType::Mesh) {
        inputMeshValue = nullptr;
    }
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;
    const std::array<float, 16> localTransform = buildLocalTransformArray(context.node);

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = context.node.outputs[outputIndex].contract.producedPayloadType;
        outputValue.payloadHandle = {};
        const uint64_t payloadKey = makeSocketKey(
            context.node.id,
            context.node.outputs[outputIndex].id);

        if (!payloadRegistry || !inputMeshValue || inputMeshValue->payloadHandle.key == 0) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        const GeometryData* inputGeometry = payloadRegistry->resolveGeometry(inputMeshValue->dataType, inputMeshValue->payloadHandle);
        if (!inputGeometry) {
            populateMetadata(outputValue, payloadRegistry);
            continue;
        }

        GeometryData forwardedGeometry = *inputGeometry;
        forwardedGeometry.localToWorld = NodeModelTransform::toMatrixArray(
            NodeModelTransform::toMat4(forwardedGeometry.localToWorld) *
            NodeModelTransform::toMat4(localTransform));
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, std::move(forwardedGeometry));
        populateMetadata(outputValue, payloadRegistry);
    }
}

bool NodeTransform::computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
    const NodeGraphSocket* meshSocket = findInputSocket(context.node, "Mesh");
    const EvaluatedSocketValue* inputMesh =
        meshSocket ? readEvaluatedInput(context.node, meshSocket->id, context.executionState) : nullptr;
    const NodeDataBlock* inputMeshValue = readInputValue(inputMesh);
    if (inputMeshValue && valueTypeOf(inputMeshValue->dataType) != NodeGraphValueType::Mesh) {
        inputMeshValue = nullptr;
    }

    outHash = NodeGraphHash::start();
    NodeGraphHash::combine(outHash, static_cast<uint64_t>(context.node.id.value));
    NodeGraphHash::combineInputHash(outHash, inputMeshValue);
    combineTransformParams(context.node, outHash);
    return true;
}
