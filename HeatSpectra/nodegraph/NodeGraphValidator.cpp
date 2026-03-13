#include "NodeGraphValidator.hpp"

#include <algorithm>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

NodeDataType inferDataTypeFromSocketValueType(NodeGraphValueType valueType) {
    switch (valueType) {
    case NodeGraphValueType::Mesh:
        return NodeDataType::Geometry;
    case NodeGraphValueType::HeatReceiver:
        return NodeDataType::HeatReceiver;
    case NodeGraphValueType::HeatSource:
        return NodeDataType::HeatSource;
    case NodeGraphValueType::ContactPair:
        return NodeDataType::ContactPair;
    case NodeGraphValueType::ScalarFloat:
        return NodeDataType::ScalarFloat;
    case NodeGraphValueType::ScalarInt:
        return NodeDataType::ScalarInt;
    case NodeGraphValueType::ScalarBool:
        return NodeDataType::ScalarBool;
    case NodeGraphValueType::Point:
    case NodeGraphValueType::Vector3:
    case NodeGraphValueType::Unknown:
    default:
        return NodeDataType::None;
    }
}

const char* nodeDataTypeName(NodeDataType dataType) {
    switch (dataType) {
    case NodeDataType::Geometry:
        return "geometry";
    case NodeDataType::HeatReceiver:
        return "heat_receiver";
    case NodeDataType::HeatSource:
        return "heat_source";
    case NodeDataType::ContactPair:
        return "contact_pair";
    case NodeDataType::ScalarFloat:
        return "scalar_float";
    case NodeDataType::ScalarInt:
        return "scalar_int";
    case NodeDataType::ScalarBool:
        return "scalar_bool";
    case NodeDataType::None:
    default:
        return "none";
    }
}

const char* domainName(GeometryAttributeDomain domain) {
    switch (domain) {
    case GeometryAttributeDomain::Point:
        return "point";
    case GeometryAttributeDomain::Primitive:
        return "primitive";
    case GeometryAttributeDomain::Vertex:
        return "vertex";
    case GeometryAttributeDomain::Detail:
        return "detail";
    default:
        return "unknown";
    }
}

bool isAcceptedDataType(const NodeGraphSocket& inputSocket, NodeDataType dataType) {
    const std::vector<NodeDataType>& acceptedDataTypes = inputSocket.contract.acceptedDataTypes;
    if (!acceptedDataTypes.empty()) {
        return std::find(acceptedDataTypes.begin(), acceptedDataTypes.end(), dataType) != acceptedDataTypes.end();
    }

    const NodeDataType inferredInputType = inferDataTypeFromSocketValueType(inputSocket.valueType);
    if (inferredInputType == NodeDataType::None || dataType == NodeDataType::None) {
        return true;
    }

    return inferredInputType == dataType;
}

bool guaranteesAttribute(
    const NodeGraphSocket& outputSocket,
    const NodeGraphAttributeContract& requiredAttribute) {
    const auto it = std::find_if(
        outputSocket.contract.guaranteedAttributes.begin(),
        outputSocket.contract.guaranteedAttributes.end(),
        [&](const NodeGraphAttributeContract& guaranteedAttribute) {
            return guaranteedAttribute.name == requiredAttribute.name &&
                guaranteedAttribute.domain == requiredAttribute.domain &&
                guaranteedAttribute.dataType == requiredAttribute.dataType &&
                guaranteedAttribute.tupleSize >= requiredAttribute.tupleSize;
        });
    return it != outputSocket.contract.guaranteedAttributes.end();
}

} // namespace

bool NodeGraphValidator::canCreateConnection(
    const NodeGraphDocument& document,
    NodeGraphNodeId fromNode,
    NodeGraphSocketId fromSocket,
    NodeGraphNodeId toNode,
    NodeGraphSocketId toSocket,
    std::string& errorMessage,
    NodeGraphEdgeId ignoreExistingEdge) {
    errorMessage.clear();

    if (!fromNode.isValid() || !toNode.isValid()) {
        errorMessage = "Connection uses an invalid node id.";
        return false;
    }

    if (fromNode == toNode) {
        errorMessage = "A node cannot connect to itself.";
        return false;
    }

    const NodeGraphNode* srcNode = document.findNode(fromNode);
    const NodeGraphNode* dstNode = document.findNode(toNode);
    if (!srcNode || !dstNode) {
        errorMessage = "Connection references a missing node.";
        return false;
    }

    const NodeGraphSocket* srcSocket = document.findOutputSocket(fromNode, fromSocket);
    const NodeGraphSocket* dstSocket = document.findInputSocket(toNode, toSocket);
    if (!srcSocket || !dstSocket) {
        errorMessage = "Connection references a missing input or output socket.";
        return false;
    }

    if (srcSocket->valueType != NodeGraphValueType::Unknown &&
        dstSocket->valueType != NodeGraphValueType::Unknown &&
        srcSocket->valueType != dstSocket->valueType) {
        errorMessage = "Socket type mismatch in connection.";
        return false;
    }

    NodeDataType producedDataType = srcSocket->contract.producedDataType;
    if (producedDataType == NodeDataType::None) {
        producedDataType = inferDataTypeFromSocketValueType(srcSocket->valueType);
    }
    if (!isAcceptedDataType(*dstSocket, producedDataType)) {
        errorMessage = "Data contract mismatch: output '" + srcSocket->name + "' provides '" +
            std::string(nodeDataTypeName(producedDataType)) + "' but input '" + dstSocket->name +
            "' does not accept it.";
        return false;
    }

    for (const NodeGraphAttributeContract& requiredAttribute : dstSocket->contract.requiredAttributes) {
        if (guaranteesAttribute(*srcSocket, requiredAttribute)) {
            continue;
        }

        errorMessage = "Data contract mismatch: input '" + dstSocket->name + "' requires attribute '" +
            requiredAttribute.name + "' on " + domainName(requiredAttribute.domain) +
            " domain, but output '" + srcSocket->name + "' does not guarantee it.";
        return false;
    }

    const auto duplicateEdgeIt = std::find_if(document.getEdges().begin(), document.getEdges().end(), [&](const NodeGraphEdge& edge) {
        if (ignoreExistingEdge.isValid() && edge.id == ignoreExistingEdge) {
            return false;
        }
        return edge.fromNode == fromNode && edge.fromSocket == fromSocket && edge.toNode == toNode && edge.toSocket == toSocket;
    });
    if (duplicateEdgeIt != document.getEdges().end()) {
        errorMessage = "Connection already exists.";
        return false;
    }

    const auto inputAlreadyConnectedIt = std::find_if(document.getEdges().begin(), document.getEdges().end(), [&](const NodeGraphEdge& edge) {
        if (ignoreExistingEdge.isValid() && edge.id == ignoreExistingEdge) {
            return false;
        }
        return edge.toNode == toNode && edge.toSocket == toSocket;
    });
    if (inputAlreadyConnectedIt != document.getEdges().end()) {
        errorMessage = "Input socket already has a connection.";
        return false;
    }

    if (wouldIntroduceCycle(document, fromNode, toNode)) {
        errorMessage = "Connection would introduce a cycle.";
        return false;
    }

    return true;
}

bool NodeGraphValidator::wouldIntroduceCycle(
    const NodeGraphDocument& document,
    NodeGraphNodeId fromNode,
    NodeGraphNodeId toNode) {
    std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;
    for (const NodeGraphEdge& edge : document.getEdges()) {
        adjacency[edge.fromNode.value].push_back(edge.toNode.value);
    }

    std::queue<uint32_t> pending;
    std::unordered_set<uint32_t> visited;
    pending.push(toNode.value);

    while (!pending.empty()) {
        const uint32_t node = pending.front();
        pending.pop();

        if (!visited.insert(node).second) {
            continue;
        }

        if (node == fromNode.value) {
            return true;
        }

        const auto adjIt = adjacency.find(node);
        if (adjIt == adjacency.end()) {
            continue;
        }

        for (uint32_t next : adjIt->second) {
            pending.push(next);
        }
    }

    return false;
}
