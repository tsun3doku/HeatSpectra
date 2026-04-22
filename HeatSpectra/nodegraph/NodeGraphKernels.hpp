#pragma once

#include "NodeGraphDataTypes.hpp"
#include "NodeGraphPayloadTypes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class NodeGraphBridge;
class HeatSystemComputeController;
class NodeGraphRuntimeBridge;
class NodePayloadRegistry;
class Remesher;
class ModelRegistry;
class RuntimeModelComputeTransport;
class RuntimeRemeshComputeTransport;
class RuntimeVoronoiComputeTransport;
class RuntimeContactComputeTransport;
class RuntimeHeatComputeTransport;
class RuntimeModelDisplayTransport;
class RuntimeRemeshDisplayTransport;
class RuntimeVoronoiDisplayTransport;
class RuntimeContactDisplayTransport;
class RuntimeHeatDisplayTransport;
class SceneController;
class RenderSettingsController;

struct NodeRuntimeServices {
    SceneController* sceneController = nullptr;
    RuntimeModelComputeTransport* modelComputeTransport = nullptr;
    RuntimeRemeshComputeTransport* remeshComputeTransport = nullptr;
    RuntimeVoronoiComputeTransport* voronoiComputeTransport = nullptr;
    RuntimeContactComputeTransport* contactComputeTransport = nullptr;
    RuntimeHeatComputeTransport* heatComputeTransport = nullptr;
    RuntimeModelDisplayTransport* modelDisplayTransport = nullptr;
    RuntimeRemeshDisplayTransport* remeshDisplayTransport = nullptr;
    RuntimeVoronoiDisplayTransport* voronoiDisplayTransport = nullptr;
    RuntimeContactDisplayTransport* contactDisplayTransport = nullptr;
    RuntimeHeatDisplayTransport* heatDisplayTransport = nullptr;
    HeatSystemComputeController* heatSystemController = nullptr;
    RenderSettingsController* renderSettingsController = nullptr;
    NodePayloadRegistry* payloadRegistry = nullptr;
    NodeGraphRuntimeBridge* runtimeBridge = nullptr;
    ModelRegistry* resourceManager = nullptr;
    Remesher* remesher = nullptr;
};

struct NodeGraphKernelExecutionState {
    const NodeGraphState& state;
    NodeGraphBridge& bridge;
    const NodeRuntimeServices& services;
    const std::unordered_map<uint64_t, const NodeGraphEdge*>& incomingEdgeByInputSocket;
    const std::unordered_map<uint64_t, EvaluatedSocketValue>& outputBySocket;
};

struct NodeGraphKernelContext {
    const NodeGraphNode& node;
    const std::vector<const EvaluatedSocketValue*>& inputs;
    std::vector<NodeDataBlock>& outputs;
    const NodeGraphKernelExecutionState& executionState;
};

struct NodeGraphKernelHashContext {
    const NodeGraphNode& node;
    const std::vector<const EvaluatedSocketValue*>& inputs;
    const NodeGraphKernelExecutionState& executionState;
};

class NodeKernel {
public:
    virtual ~NodeKernel() = default;
    virtual const char* typeId() const = 0;
    virtual void execute(NodeGraphKernelContext& context) const = 0;
    virtual bool computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const {
        (void)context;
        (void)outHash;
        return false;
    }
};

class NodeGraphKernels {
public:
    NodeGraphKernels();

    bool hasKernel(const NodeTypeId& typeId) const;
    bool computeInputHash(
        const NodeGraphNode& node,
        const NodeGraphKernelExecutionState& executionState,
        const std::vector<const EvaluatedSocketValue*>& inputs,
        uint64_t& outHash) const;
    void executeNode(
        const NodeGraphNode& node,
        const NodeGraphKernelExecutionState& executionState,
        const std::vector<const EvaluatedSocketValue*>& inputs,
        std::vector<NodeDataBlock>& outputs) const;

private:
    void registerDefaultKernels();
    void registerKernel(std::unique_ptr<NodeKernel> kernel);

    std::vector<std::unique_ptr<NodeKernel>> kernels;
    std::unordered_map<NodeTypeId, const NodeKernel*> kernelByTypeId;
};

