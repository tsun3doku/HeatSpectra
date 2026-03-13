#pragma once

#include "NodeGraphDataTypes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class NodeGraphBridge;
class HeatSystemController;
class ContactSystemController;
class MeshModifiers;
class ModelRegistry;
class NodeSolverController;
class ResourceManager;
class SceneController;

struct NodeRuntimeServices {
    ModelRegistry* modelRegistry = nullptr;
    SceneController* sceneController = nullptr;
    HeatSystemController* heatSystemController = nullptr;
    ContactSystemController* contactSystemController = nullptr;
    NodeSolverController* nodeSolverController = nullptr;
    ResourceManager* resourceManager = nullptr;
    MeshModifiers* meshModifiers = nullptr;
};

struct NodeGraphKernelExecutionState {
    const NodeGraphState& state;
    NodeGraphBridge& bridge;
    const NodeRuntimeServices& services;
    const std::unordered_map<uint64_t, const NodeGraphEdge*>& incomingEdgeByInputSocket;
    const std::unordered_map<uint64_t, NodeDataBlock>& outputValueBySocket;
};

struct NodeGraphKernelContext {
    const NodeGraphNode& node;
    const std::vector<const NodeDataBlock*>& inputs;
    std::vector<NodeDataBlock>& outputs;
    const NodeGraphKernelExecutionState& executionState;
};

class NodeKernel {
public:
    virtual ~NodeKernel() = default;
    virtual const char* typeId() const = 0;
    virtual bool execute(NodeGraphKernelContext& context) const = 0;
};

class NodeGraphKernels {
public:
    NodeGraphKernels();

    bool hasKernel(const NodeTypeId& typeId) const;
    bool executeNode(
        const NodeGraphNode& node,
        const NodeGraphKernelExecutionState& executionState,
        const std::vector<const NodeDataBlock*>& inputs,
        std::vector<NodeDataBlock>& outputs) const;

private:
    void registerDefaultKernels();
    void registerKernel(std::unique_ptr<NodeKernel> kernel);
    static void normalizeOutputsToSocketContracts(const NodeGraphNode& node, std::vector<NodeDataBlock>& outputs);
    static bool hasGuaranteedAttribute(
        const GeometryData& geometry,
        const NodeGraphAttributeContract& guaranteedAttribute);
    static std::size_t attributeElementCount(
        const GeometryData& geometry,
        GeometryAttributeDomain domain);
    static void ensureGuaranteedAttribute(
        GeometryData& geometry,
        const NodeGraphAttributeContract& guaranteedAttribute);
    static void resizeAttributeStorage(
        GeometryAttribute& attribute,
        GeometryAttributeDataType dataType,
        std::size_t elementCount,
        uint32_t tupleSize);

    std::vector<std::unique_ptr<NodeKernel>> kernels;
    std::unordered_map<NodeTypeId, const NodeKernel*> kernelByTypeId;
};
