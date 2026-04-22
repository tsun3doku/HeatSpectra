#include "NodeGraphTypes.hpp"

#include <algorithm>

NodeGraphValueType valueTypeOf(NodePayloadType payloadType) {
    switch (payloadType) {
    case NodePayloadType::Geometry:
    case NodePayloadType::Remesh:
        return NodeGraphValueType::Mesh;
    case NodePayloadType::HeatSource:
        return NodeGraphValueType::Emitter;
    case NodePayloadType::HeatReceiver:
        return NodeGraphValueType::Receiver;
    case NodePayloadType::Voronoi:
        return NodeGraphValueType::Volume;
    case NodePayloadType::Contact:
        return NodeGraphValueType::Field;
    case NodePayloadType::Heat:
    case NodePayloadType::None:
    default:
        return NodeGraphValueType::None;
    }
}

bool acceptsPayload(NodeGraphValueType valueType, NodePayloadType payloadType) {
    if (payloadType == NodePayloadType::None || valueType == NodeGraphValueType::None) {
        return false;
    }

    return valueTypeOf(payloadType) == valueType;
}

bool acceptsPayload(const NodeGraphSocket& socket, NodePayloadType payloadType) {
    return acceptsPayload(socket.valueType, payloadType);
}

bool producesPayload(const NodeGraphSocket& socket, NodePayloadType payloadType) {
    return socket.contract.producedPayloadType == payloadType;
}

const char* nodePayloadTypeName(NodePayloadType payloadType) {
    switch (payloadType) {
    case NodePayloadType::Geometry:
        return "geometry";
    case NodePayloadType::Remesh:
        return "remesh";
    case NodePayloadType::HeatReceiver:
        return "heat_receiver";
    case NodePayloadType::HeatSource:
        return "heat_source";
    case NodePayloadType::Contact:
        return "contact";
    case NodePayloadType::Heat:
        return "heat";
    case NodePayloadType::Voronoi:
        return "voronoi";
    case NodePayloadType::None:
    default:
        return "none";
    }
}
