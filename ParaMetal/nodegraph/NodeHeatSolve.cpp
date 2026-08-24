#include "NodeHeatSolve.hpp"
#include "NodeGraphPayloadTypes.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "hash/HashBuilder.hpp"
#include "hash/HashNodeCache.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "NodeHeatSolveParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "domain/HeatModelData.hpp"
#include "domain/VoronoiData.hpp"

#include <cstdint>
#include <vector>

const char* NodeHeatSolve::typeId() const {
    return nodegraphtypes::HeatSolve;
}

void NodeHeatSolve::execute(NodeKernelEval& eval) const {
    NodeDataHandle domainVoronoiHandle;
    std::vector<NodeDataHandle> heatModelHandles;
    uint32_t activeVoronoiCount = 0;
    uint32_t activeGlobalVoronoiCount = 0;
    const HeatSolveNodeParams params = readHeatSolveNodeParams(eval.node);

    const NodeGraphNodeId activeNodeId = selectHeatSolveNode(eval.runtime.graph);
    NodePayloadRegistry* const payloadRegistry = eval.runtime.payloadRegistry;

    const std::size_t voronoiSocketIndex = inputIndexOf(eval.node, NodeGraphValueType::Volume);
    if (voronoiSocketIndex < eval.inputs.size()) {
        for (const NodeDataBlock* block : eval.inputs[voronoiSocketIndex]) {
            if (!block || block->dataType != payloadtypes::Voronoi || block->payloadHandle.key == 0) {
                continue;
            }
            const VoronoiData* voronoi = payloadRegistry->get<VoronoiData>(block->payloadHandle);
            if (voronoi && voronoi->active) {
                ++activeVoronoiCount;
                if (voronoi->domainType == DomainType::Global) {
                    ++activeGlobalVoronoiCount;
                    if (domainVoronoiHandle.key == 0) {
                        domainVoronoiHandle = block->payloadHandle;
                    }
                }
            }
        }
    }

    const std::size_t heatModelSocketIndex = inputIndexOf(eval.node, NodeGraphValueType::HeatModel);
    if (heatModelSocketIndex < eval.inputs.size()) {
        for (const NodeDataBlock* block : eval.inputs[heatModelSocketIndex]) {
            if (!block || block->dataType != payloadtypes::HeatModel || block->payloadHandle.key == 0) {
                continue;
            }
            const HeatModelData* heatModel = payloadRegistry->get<HeatModelData>(block->payloadHandle);
            if (heatModel && heatModel->meshHandle.key != 0) {
                heatModelHandles.push_back(block->payloadHandle);
            }
        }
    }

    const bool active = activeNodeId.isValid() && activeNodeId == eval.node.id;
    const bool hasValidInputs = domainVoronoiHandle.key != 0 && !heatModelHandles.empty();

    HeatData heatData{};
    heatData.domainVoronoiHandle = domainVoronoiHandle;
    heatData.heatModelHandles = heatModelHandles;
    heatData.activeVoronoiCount = activeVoronoiCount;
    heatData.activeGlobalVoronoiCount = activeGlobalVoronoiCount;
    heatData.contactThermalConductance = static_cast<float>(params.contactThermalConductance);
    heatData.simulationDuration = static_cast<float>(params.simulationDuration);
    heatData.active = hasValidInputs && active;

    for (std::size_t outputIndex = 0; outputIndex < eval.outputs.size() && outputIndex < eval.node.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = eval.outputs[outputIndex];
        const NodeGraphSocket& outputSocket = eval.node.outputs[outputIndex];
        outputValue = {};
        outputValue.dataType = outputSocket.contract.producedPayloadType;

        if (!payloadRegistry || outputSocket.contract.producedPayloadType != payloadtypes::Heat) {
            populateMetadata(outputValue, nullptr, payloadRegistry);
            continue;
        }

        const uint64_t payloadKey = NodeSocketKey(eval.node.id, outputSocket.id);
        outputValue.payloadHandle = payloadRegistry->store(payloadKey, heatData, eval.outputHashes);
        populateMetadata(outputValue, nullptr, payloadRegistry);
    }
}

HashValues NodeHeatSolve::computeOutputHashes(const NodeKernelHash& hash) const {
    const NodeGraphNodeId activeNodeId = selectHeatSolveNode(hash.runtime.graph);
    uint64_t simulationHash = HashBuilder::start();
    HashBuilder::combineString(simulationHash, nodegraphtypes::HeatSolve);
    HashNodeCache::combineSocket(simulationHash, hash, NodeGraphValueType::Volume, HashDomain::Simulation);
    HashNodeCache::combineOptionalSocketList(simulationHash, hash, NodeGraphValueType::HeatModel, HashDomain::Simulation);

    const HeatSolveNodeParams params = readHeatSolveNodeParams(hash.node);
    HashBuilder::combineFloat(simulationHash, static_cast<float>(params.contactThermalConductance));
    HashBuilder::combineFloat(simulationHash, static_cast<float>(params.simulationDuration));
    const bool active = activeNodeId.isValid() && activeNodeId == hash.node.id;
    HashBuilder::combine(simulationHash, static_cast<uint64_t>(active ? 1u : 0u));

    HashValues values{};
    values.full = simulationHash;
    values.simulation = simulationHash;
    return values;
}

NodeGraphNodeId NodeHeatSolve::selectHeatSolveNode(
    const NodeGraphState& state) {
    NodeGraphNodeId selectedNodeId{};
    for (const auto& [id, node] : state.nodes) {
        if (getNodeTypeId(node.typeId) != nodegraphtypes::HeatSolve) {
            continue;
        }

        const HeatSolveNodeParams params = readHeatSolveNodeParams(node);
        if (!params.enabled) {
            continue;
        }

        if (selectedNodeId.isValid()) {
            return {};
        }

        selectedNodeId = node.id;
    }

    return selectedNodeId;
}
