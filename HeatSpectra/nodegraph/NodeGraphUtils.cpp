#include "NodeGraphUtils.hpp"
#include "NodeGraphKernels.hpp"
#include "NodeGraphRegistry.hpp"
#include <sstream>

uint64_t makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId) {
    return (static_cast<uint64_t>(nodeId.value) << 32) | static_cast<uint64_t>(socketId.value);
}

const NodeGraphNode* findNodeInState(const NodeGraphState& state, NodeGraphNodeId nodeId) {
    for (const NodeGraphNode& node : state.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const NodeGraphEdge* findIncomingEdgeInState(
    const NodeGraphState& state,
    NodeGraphNodeId toNodeId,
    NodeGraphSocketId toSocketId) {
    for (const NodeGraphEdge& edge : state.edges) {
        if (edge.toNode == toNodeId && edge.toSocket == toSocketId) {
            return &edge;
        }
    }
    return nullptr;
}

const NodeGraphSocket* findInputSocket(const NodeGraphNode& node, const char* socketName) {
    for (const NodeGraphSocket& inputSocket : node.inputs) {
        if (inputSocket.direction != NodeGraphSocketDirection::Input) {
            continue;
        }
        if (inputSocket.name == socketName) {
            return &inputSocket;
        }
    }
    return nullptr;
}

const NodeGraphSocket* findInputSocket(const NodeGraphNode& node, NodeGraphValueType valueType) {
    for (const NodeGraphSocket& inputSocket : node.inputs) {
        if (inputSocket.direction != NodeGraphSocketDirection::Input) {
            continue;
        }
        if (inputSocket.valueType == valueType) {
            return &inputSocket;
        }
    }
    return nullptr;
}

const NodeDataBlock* readInput(
    const NodeGraphNode& node,
    NodeGraphSocketId inputSocketId,
    const NodeGraphKernelExecutionState& executionState) {
    const auto edgeIt = executionState.incomingEdgeByInputSocket.find(makeSocketKey(node.id, inputSocketId));
    if (edgeIt == executionState.incomingEdgeByInputSocket.end() || !edgeIt->second) {
        return nullptr;
    }

    const NodeGraphEdge& edge = *edgeIt->second;
    const auto valueIt = executionState.outputValueBySocket.find(makeSocketKey(edge.fromNode, edge.fromSocket));
    if (valueIt == executionState.outputValueBySocket.end()) {
        return nullptr;
    }

    return &valueIt->second;
}

NodeTypeId getNodeTypeId(const NodeTypeId& requestedTypeId) {
    if (const NodeTypeDefinition* definition = NodeGraphRegistry::findNodeById(requestedTypeId)) {
        return definition->id;
    }
    return nodegraphtypes::Custom;
}

std::string displayNodeLabel(const NodeGraphNode& node) {
    if (!node.title.empty()) {
        return node.title;
    }

    if (!node.typeId.empty()) {
        return node.typeId;
    }

    std::ostringstream ss;
    ss << "node#" << node.id.value;
    return ss.str();
}

const NodeGraphParamDefinition* findNodeParamDefinition(const NodeTypeDefinition& definition, uint32_t paramId) {
    for (const NodeGraphParamDefinition& parameter : definition.parameters) {
        if (parameter.id == paramId) {
            return &parameter;
        }
    }
    return nullptr;
}

NodeGraphParamValue makeNodeGraphParamValue(const NodeGraphParamDefinition& definition) {
    NodeGraphParamValue value{};
    value.id = definition.id;
    value.type = definition.type;
    value.floatValue = definition.defaultFloatValue;
    value.intValue = definition.defaultIntValue;
    value.boolValue = definition.defaultBoolValue;
    value.stringValue = definition.defaultStringValue;
    return value;
}

const NodeGraphParamValue* findNodeParamValue(const NodeGraphNode& node, uint32_t paramId) {
    for (const NodeGraphParamValue& parameter : node.parameters) {
        if (parameter.id == paramId) {
            return &parameter;
        }
    }
    return nullptr;
}

NodeGraphParamValue* findNodeParamValue(NodeGraphNode& node, uint32_t paramId) {
    for (NodeGraphParamValue& parameter : node.parameters) {
        if (parameter.id == paramId) {
            return &parameter;
        }
    }
    return nullptr;
}

bool tryGetNodeParamFloat(const NodeGraphNode& node, uint32_t paramId, double& outValue) {
    const NodeGraphParamValue* parameter = findNodeParamValue(node, paramId);
    if (!parameter || parameter->type != NodeGraphParamType::Float) {
        return false;
    }
    outValue = parameter->floatValue;
    return true;
}

bool tryGetNodeParamInt(const NodeGraphNode& node, uint32_t paramId, int64_t& outValue) {
    const NodeGraphParamValue* parameter = findNodeParamValue(node, paramId);
    if (!parameter || parameter->type != NodeGraphParamType::Int) {
        return false;
    }
    outValue = parameter->intValue;
    return true;
}

bool tryGetNodeParamBool(const NodeGraphNode& node, uint32_t paramId, bool& outValue) {
    const NodeGraphParamValue* parameter = findNodeParamValue(node, paramId);
    if (!parameter || parameter->type != NodeGraphParamType::Bool) {
        return false;
    }
    outValue = parameter->boolValue;
    return true;
}

bool tryGetNodeParamString(const NodeGraphNode& node, uint32_t paramId, std::string& outValue) {
    const NodeGraphParamValue* parameter = findNodeParamValue(node, paramId);
    if (!parameter || parameter->type != NodeGraphParamType::String) {
        return false;
    }
    outValue = parameter->stringValue;
    return true;
}
