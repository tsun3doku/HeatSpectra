#include "NodeTransform.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "NodePayloadRegistry.hpp"
#include "NodeTransformParams.hpp"
#include "domain/PointData.hpp"
#include "domain/TransformData.hpp"
#include "../util/GeometryUtils.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <utility>

const char* NodeTransform::typeId() const {
    return nodegraphtypes::Transform;
}

void NodeTransform::execute(NodeKernelEval& eval) const {
    const std::size_t geometrySocketIndex = inputIndexOf(eval.node, "Geometry");
    const NodeDataBlock* inputData =
        (geometrySocketIndex < eval.inputs.size() && !eval.inputs[geometrySocketIndex].empty())
            ? eval.inputs[geometrySocketIndex].front() : nullptr;

    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;
    const std::array<float, 16> localTransform = buildLocalTransformArray(eval.node);

    NodeDataBlock& outputValue = eval.outputs[0];
    outputValue = {};

    if (payloadRegistry && inputData && inputData->payloadHandle.key != 0) {
        std::array<float, 16> sourceLocalToWorld{};
        const bool hasGeometry = payloadRegistry->resolveGeometry(inputData->payloadHandle) != nullptr;
        const bool hasPoints = payloadRegistry->resolvePoints(inputData->payloadHandle) != nullptr;
        if ((hasGeometry || hasPoints) &&
            payloadRegistry->resolveLocalToWorld(inputData->payloadHandle, sourceLocalToWorld)) {
            TransformData transform{};
            transform.sourceHandle = inputData->payloadHandle;
            transform.localToWorld = toMatrixArray(
                toMat4(sourceLocalToWorld) * toMat4(localTransform));
            const uint64_t payloadKey = NodeSocketKey(eval.node.id, eval.node.outputs[0].id);
            outputValue.dataType = inputData->dataType;
            outputValue.payloadHandle = payloadRegistry->store(payloadKey, transform, eval.outputHashes);
        }
    }

    populateMetadata(outputValue, nullptr, payloadRegistry);
}

HashValues NodeTransform::computeOutputHashes(const NodeKernelHash& hash) const {
    uint64_t hashValue = HashBuilder::start();
    HashBuilder::combineString(hashValue, nodegraphtypes::Transform);
    HashNodeCache::combineOptionalSocket(hashValue, hash, "Geometry", HashDomain::Geometry);
    combineTransformParams(hash.node, hashValue);

    HashValues values{};
    values.full = hashValue;
    values.geometry = hashValue;
    values.simulation = hashValue;
    return values;
}

glm::mat4 NodeTransform::buildLocalTransform(const NodeGraphNode& node) {
    return buildLocalTransform(readTransformNodeParams(node));
}

glm::mat4 NodeTransform::buildLocalTransform(const TransformNodeParams& params) {
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

void NodeTransform::combineTransformParams(const NodeGraphNode& node, uint64_t& outHash) {
    const TransformNodeParams params = readTransformNodeParams(node);
    HashBuilder::combineFloat(outHash, static_cast<float>(params.translateX));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.translateY));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.translateZ));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.rotateXDegrees));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.rotateYDegrees));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.rotateZDegrees));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.scaleX));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.scaleY));
    HashBuilder::combineFloat(outHash, static_cast<float>(params.scaleZ));
}

std::array<float, 16> NodeTransform::buildLocalTransformArray(const NodeGraphNode& node) {
    return toMatrixArray(buildLocalTransform(node));
}
