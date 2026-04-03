#include "NodeGraphKernels.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeContact.hpp"
#include "NodeHeatReceiver.hpp"
#include "NodeHeatSolve.hpp"
#include "NodeHeatSource.hpp"
#include "NodeVoronoi.hpp"
#include "NodeGroup.hpp"
#include "NodeModel.hpp"
#include "NodeTransform.hpp"
#include "NodeRemesh.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodePayloadRegistry.hpp"

#include <algorithm>
#include <utility>

NodeGraphKernels::NodeGraphKernels() {
    registerDefaultKernels();
}

bool NodeGraphKernels::hasKernel(const NodeTypeId& typeId) const {
    return kernelByTypeId.find(getNodeTypeId(typeId)) != kernelByTypeId.end();
}

bool NodeGraphKernels::computeInputHash(
    const NodeGraphNode& node,
    const NodeGraphKernelExecutionState& executionState,
    const std::vector<const EvaluatedSocketValue*>& inputs,
    uint64_t& outHash) const {
    const NodeTypeId canonicalTypeId = getNodeTypeId(node.typeId);
    const auto kernelIt = kernelByTypeId.find(canonicalTypeId);
    if (kernelIt == kernelByTypeId.end() || !kernelIt->second) {
        return false;
    }

    NodeGraphKernelHashContext context{node, inputs, executionState};
    return kernelIt->second->computeInputHash(context, outHash);
}

bool NodeGraphKernels::executeNode(
    const NodeGraphNode& node,
    const NodeGraphKernelExecutionState& executionState,
    const std::vector<const EvaluatedSocketValue*>& inputs,
    std::vector<NodeDataBlock>& outputs) const {
    const NodeTypeId canonicalTypeId = getNodeTypeId(node.typeId);
    const auto kernelIt = kernelByTypeId.find(canonicalTypeId);
    if (kernelIt == kernelByTypeId.end() || !kernelIt->second) {
        return false;
    }

    NodeGraphKernelContext context{
        node,
        inputs,
        outputs,
        executionState};

    const bool executed = kernelIt->second->execute(context);
    normalizeOutputsToSocketContracts(node, outputs, executionState.services.payloadRegistry);
    return executed;
}

void NodeGraphKernels::registerDefaultKernels() {
    registerKernel(std::make_unique<NodeModel>());
    registerKernel(std::make_unique<NodeTransform>());
    registerKernel(std::make_unique<NodeGroup>());
    registerKernel(std::make_unique<NodeRemesh>());
    registerKernel(std::make_unique<NodeHeatReceiver>());
    registerKernel(std::make_unique<NodeHeatSource>());
    registerKernel(std::make_unique<NodeContact>());
    registerKernel(std::make_unique<NodeVoronoi>());
    registerKernel(std::make_unique<NodeHeatSolve>());
}

void NodeGraphKernels::registerKernel(std::unique_ptr<NodeKernel> kernel) {
    if (!kernel || !kernel->typeId() || kernel->typeId()[0] == '\0') {
        return;
    }

    const NodeTypeId canonicalTypeId = getNodeTypeId(kernel->typeId());
    kernelByTypeId[canonicalTypeId] = kernel.get();
    kernels.push_back(std::move(kernel));
}

void NodeGraphKernels::normalizeOutputsToSocketContracts(const NodeGraphNode& node, std::vector<NodeDataBlock>& outputs, NodePayloadRegistry* payloadRegistry) {
    const std::size_t outputCount = std::min(outputs.size(), node.outputs.size());
    for (std::size_t outputIndex = 0; outputIndex < outputCount; ++outputIndex) {
        NodeDataBlock& output = outputs[outputIndex];
        const NodeGraphSocket& socket = node.outputs[outputIndex];
        const NodeGraphSocketContract& contract = socket.contract;

        if (contract.producedPayloadType != NodePayloadType::None && output.dataType != NodePayloadType::None) {
            output.dataType = contract.producedPayloadType;
        }

        if (output.dataType == contract.producedPayloadType &&
            payloadRegistry &&
            output.payloadHandle.key != 0 &&
            (output.dataType == NodePayloadType::Geometry ||
             output.dataType == NodePayloadType::HeatReceiver ||
             output.dataType == NodePayloadType::HeatSource)) {
            const GeometryData* geometry = payloadRegistry->get<GeometryData>(output.payloadHandle);
            if (geometry) {
                GeometryData updated = *geometry;
                bool changed = false;
                for (const NodeGraphAttributeContract& guaranteedAttribute : contract.guaranteedAttributes) {
                    if (ensureGuaranteedAttribute(updated, guaranteedAttribute)) {
                        changed = true;
                    }
                }
                if (changed) {
                    updatePayloadHash(updated);
                    output.payloadHandle = payloadRegistry->upsert(output.payloadHandle.key, std::move(updated));
                }
            }
        }

        updateDataBlockMetadata(output, payloadRegistry);
    }
}

bool NodeGraphKernels::hasGuaranteedAttribute(const GeometryData& geometry, const NodeGraphAttributeContract& guaranteedAttribute) {
    const auto it = std::find_if(
        geometry.attributes.begin(),
        geometry.attributes.end(),
        [&](const GeometryAttribute& attribute) {
            return attribute.name == guaranteedAttribute.name &&
                attribute.domain == guaranteedAttribute.domain &&
                attribute.dataType == guaranteedAttribute.dataType &&
                attribute.tupleSize >= guaranteedAttribute.tupleSize;
        });
    return it != geometry.attributes.end();
}

std::size_t NodeGraphKernels::attributeElementCount(const GeometryData& geometry, GeometryAttributeDomain domain) {
    switch (domain) {
    case GeometryAttributeDomain::Point:
        return geometry.pointPositions.size() / 3;
    case GeometryAttributeDomain::Primitive:
        return geometry.triangleIndices.size() / 3;
    case GeometryAttributeDomain::Vertex:
        return geometry.triangleIndices.size();
    case GeometryAttributeDomain::Detail:
    default:
        return 1;
    }
}

void NodeGraphKernels::resizeAttributeStorage(GeometryAttribute& attribute, GeometryAttributeDataType dataType,std::size_t elementCount, uint32_t tupleSize) {
    const std::size_t valueCount = elementCount * static_cast<std::size_t>(tupleSize);
    attribute.floatValues.clear();
    attribute.intValues.clear();
    attribute.boolValues.clear();

    switch (dataType) {
    case GeometryAttributeDataType::Int:
        attribute.intValues.assign(valueCount, 0);
        break;
    case GeometryAttributeDataType::Bool:
        attribute.boolValues.assign(valueCount, 0);
        break;
    case GeometryAttributeDataType::Float:
    default:
        attribute.floatValues.assign(valueCount, 0.0f);
        break;
    }
}

bool NodeGraphKernels::ensureGuaranteedAttribute(GeometryData& geometry, const NodeGraphAttributeContract& guaranteedAttribute) {
    if (hasGuaranteedAttribute(geometry, guaranteedAttribute)) {
        return false;
    }

    GeometryAttribute attribute{};
    attribute.name = guaranteedAttribute.name;
    attribute.domain = guaranteedAttribute.domain;
    attribute.dataType = guaranteedAttribute.dataType;
    attribute.tupleSize = guaranteedAttribute.tupleSize;
    resizeAttributeStorage(attribute, guaranteedAttribute.dataType, attributeElementCount(geometry, guaranteedAttribute.domain), guaranteedAttribute.tupleSize);
    geometry.attributes.push_back(std::move(attribute));
    return true;
}
