#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct NodeGraphNodeId {
    uint32_t value = 0;

    bool isValid() const {
        return value != 0;
    }
};

struct NodeGraphSocketId {
    uint32_t value = 0;

    bool isValid() const {
        return value != 0;
    }
};

struct NodeGraphEdgeId {
    uint32_t value = 0;

    bool isValid() const {
        return value != 0;
    }
};

inline bool operator==(NodeGraphNodeId lhs, NodeGraphNodeId rhs) {
    return lhs.value == rhs.value;
}

inline bool operator!=(NodeGraphNodeId lhs, NodeGraphNodeId rhs) {
    return !(lhs == rhs);
}

inline bool operator==(NodeGraphSocketId lhs, NodeGraphSocketId rhs) {
    return lhs.value == rhs.value;
}

inline bool operator!=(NodeGraphSocketId lhs, NodeGraphSocketId rhs) {
    return !(lhs == rhs);
}

inline bool operator==(NodeGraphEdgeId lhs, NodeGraphEdgeId rhs) {
    return lhs.value == rhs.value;
}

inline bool operator!=(NodeGraphEdgeId lhs, NodeGraphEdgeId rhs) {
    return !(lhs == rhs);
}

enum class NodeGraphValueType : uint8_t {
    None,
    Mesh,
    Emitter,
    Receiver,
    Volume,
    Field,
    Vector3,
    ScalarFloat,
    ScalarInt,
    ScalarBool
};

enum class NodePayloadType : uint8_t {
    None = 0,
    Geometry = 1,
    Remesh = 2,
    HeatReceiver = 3,
    HeatSource = 4,
    Heat = 5,
    Voronoi = 6,
    Contact = 7
};

enum class GeometryAttributeDomain : uint8_t {
    Point = 0,
    Primitive = 1,
    Vertex = 2,
    Detail = 3
};

enum class GeometryAttributeDataType : uint8_t {
    Float = 0,
    Int = 1,
    Bool = 2
};

enum class NodeGraphSocketDirection {
    Input,
    Output
};

enum class NodeGraphNodeCategory {
    Model,
    PointSurface,
    Meshing,
    System,
    Custom
};

using NodeTypeId = std::string;

struct NodeGraphAttributeContract {
    std::string name;
    GeometryAttributeDomain domain = GeometryAttributeDomain::Point;
    GeometryAttributeDataType dataType = GeometryAttributeDataType::Float;
    uint32_t tupleSize = 1;
};

struct NodeGraphSocketContract {
    NodePayloadType producedPayloadType = NodePayloadType::None;
    std::vector<NodeGraphAttributeContract> requiredAttributes;
    std::vector<NodeGraphAttributeContract> guaranteedAttributes;
};

struct NodeSocketSignature {
    std::string name;
    NodeGraphSocketDirection direction = NodeGraphSocketDirection::Input;
    NodeGraphValueType valueType = NodeGraphValueType::None;
    NodeGraphSocketContract contract;
};

enum class NodeGraphParamType {
    Float,
    Int,
    Bool,
    String,
    Enum,
    Struct,
    Array
};

struct NodeGraphParamDefinition;

struct NodeGraphParamField {
    std::string name;
    std::shared_ptr<NodeGraphParamDefinition> definition;
};

struct NodeGraphParamDefinition {
    uint32_t id = 0;
    std::string name;
    NodeGraphParamType type = NodeGraphParamType::Float;
    double defaultFloatValue = 0.0;
    int64_t defaultIntValue = 0;
    bool defaultBoolValue = false;
    std::string defaultStringValue;
    bool isAction = false;
    std::vector<std::string> enumOptions;
    std::vector<NodeGraphParamField> fields;
    std::shared_ptr<NodeGraphParamDefinition> elementDefinition;
};

struct NodeGraphParamValue;

struct NodeGraphParamFieldValue {
    std::string name;
    std::shared_ptr<NodeGraphParamValue> value;
};

struct NodeGraphParamValue {
    uint32_t id = 0;
    NodeGraphParamType type = NodeGraphParamType::Float;
    double floatValue = 0.0;
    int64_t intValue = 0;
    bool boolValue = false;
    std::string stringValue;
    std::string enumValue;
    std::vector<NodeGraphParamFieldValue> fieldValues;
    std::vector<NodeGraphParamValue> arrayValues;
};

struct NodeTypeDefinition {
    NodeTypeId id;
    std::string displayName;
    NodeGraphNodeCategory category = NodeGraphNodeCategory::Custom;
    std::vector<NodeSocketSignature> sockets;
    std::vector<NodeGraphParamDefinition> parameters;
};

struct NodeGraphSocket {
    NodeGraphSocketId id{};
    std::string name;
    NodeGraphValueType valueType = NodeGraphValueType::None;
    NodeGraphSocketDirection direction = NodeGraphSocketDirection::Input;
    NodeGraphSocketContract contract;
};

struct NodeGraphNode {
    NodeGraphNodeId id{};
    NodeTypeId typeId;
    NodeGraphNodeCategory category = NodeGraphNodeCategory::Custom;
    std::string title;
    float x = 0.0f;
    float y = 0.0f;
    std::vector<NodeGraphSocket> inputs;
    std::vector<NodeGraphSocket> outputs;
    std::vector<NodeGraphParamValue> parameters;
};

struct NodeGraphEdge {
    NodeGraphEdgeId id{};
    NodeGraphNodeId fromNode{};
    NodeGraphSocketId fromSocket{};
    NodeGraphNodeId toNode{};
    NodeGraphSocketId toSocket{};
};

struct NodeGraphState {
    uint64_t revision = 0;
    std::vector<NodeGraphNode> nodes;
    std::vector<NodeGraphEdge> edges;
};

enum class NodeGraphChangeType : uint8_t {
    Reset = 0,
    NodeUpsert = 1,
    NodeRemoved = 2,
    EdgeUpsert = 3,
    EdgeRemoved = 4
};

enum class NodeGraphChangeReason : uint8_t {
    Topology = 0,
    Parameter = 1,
    Layout = 2
};

struct NodeGraphChange {
    NodeGraphChangeType type = NodeGraphChangeType::Reset;
    NodeGraphChangeReason reason = NodeGraphChangeReason::Topology;
    NodeGraphNode node{};
    NodeGraphNodeId nodeId{};
    NodeGraphEdge edge{};
    NodeGraphEdgeId edgeId{};
};

struct NodeGraphDelta {
    uint64_t fromRevision = 0;
    uint64_t toRevision = 0;
    std::vector<NodeGraphChange> changes;
};

NodeGraphValueType valueTypeOf(NodePayloadType payloadType);
bool acceptsPayload(NodeGraphValueType valueType, NodePayloadType payloadType);
bool acceptsPayload(const NodeGraphSocket& socket, NodePayloadType payloadType);
bool producesPayload(const NodeGraphSocket& socket, NodePayloadType payloadType);


