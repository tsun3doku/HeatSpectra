#pragma once

#include "NodeGraphDataTypes.hpp"
#include "NodeGraphPayloadTypes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class NodeGraphBridge;
class HeatSystemController;
class ContactSystemController;
class MeshModifiers;
class NodeGraphRuntimeBridge;
class NodePayloadRegistry;
class Remesher;
class ModelRegistry;
class RuntimePackageController;
class RuntimeProductRegistry;
class SceneController;
class RenderSettingsController;

enum class EvaluatedSocketStatus : uint8_t {
    Missing,
    Value,
    Error,
};

struct EvaluatedSocketValue {
    EvaluatedSocketStatus status = EvaluatedSocketStatus::Missing;
    NodeDataBlock data{};
    std::string error;
};

struct NodeRuntimeServices {
    SceneController* sceneController = nullptr;
    RuntimePackageController* runtimePackageController = nullptr;
    HeatSystemController* heatSystemController = nullptr;
    ContactSystemController* contactSystemController = nullptr;
    RenderSettingsController* renderSettingsController = nullptr;
    NodePayloadRegistry* payloadRegistry = nullptr;
    NodeGraphRuntimeBridge* runtimeBridge = nullptr;
    RuntimeProductRegistry* runtimeProductRegistry = nullptr;
    ModelRegistry* resourceManager = nullptr;
    MeshModifiers* meshModifiers = nullptr;
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
    virtual bool execute(NodeGraphKernelContext& context) const = 0;
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
    bool executeNode(
        const NodeGraphNode& node,
        const NodeGraphKernelExecutionState& executionState,
        const std::vector<const EvaluatedSocketValue*>& inputs,
        std::vector<NodeDataBlock>& outputs) const;

private:
    void registerDefaultKernels();
    void registerKernel(std::unique_ptr<NodeKernel> kernel);
    static void normalizeOutputsToSocketContracts(const NodeGraphNode& node, std::vector<NodeDataBlock>& outputs, NodePayloadRegistry* payloadRegistry);
    static bool hasGuaranteedAttribute(const GeometryData& geometry, const NodeGraphAttributeContract& guaranteedAttribute);
    static std::size_t attributeElementCount(const GeometryData& geometry, GeometryAttributeDomain domain);
    static bool ensureGuaranteedAttribute(GeometryData& geometry, const NodeGraphAttributeContract& guaranteedAttribute);
    static void resizeAttributeStorage(GeometryAttribute& attribute, GeometryAttributeDataType dataType, std::size_t elementCount, uint32_t tupleSize);

    std::vector<std::unique_ptr<NodeKernel>> kernels;
    std::unordered_map<NodeTypeId, const NodeKernel*> kernelByTypeId;
};

