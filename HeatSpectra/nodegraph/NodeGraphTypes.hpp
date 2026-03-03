#pragma once

#include <cstdint>
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

enum class NodeGraphValueType {
    Mesh,
    HeatReceiver,
    HeatSource,
    Point,
    Vector3,
    ScalarFloat,
    ScalarInt,
    ScalarBool,
    Unknown
};

enum class NodeDataType : uint8_t {
    None = 0,
    Geometry = 1,
    HeatReceiver = 2,
    HeatSource = 3,
    ScalarFloat = 4,
    ScalarInt = 5,
    ScalarBool = 6
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

namespace nodegraphtypes {
inline constexpr const char* Model = "model";
inline constexpr const char* Remesh = "remesh";
inline constexpr const char* HeatReceiver = "heat_receiver";
inline constexpr const char* HeatSource = "heat_source";
inline constexpr const char* HeatSolve = "heat_solve";
inline constexpr const char* Custom = "custom";
}

struct NodeGraphAttributeContract {
    std::string name;
    GeometryAttributeDomain domain = GeometryAttributeDomain::Point;
    GeometryAttributeDataType dataType = GeometryAttributeDataType::Float;
    uint32_t tupleSize = 1;
};

struct NodeGraphSocketContract {
    std::vector<NodeDataType> acceptedDataTypes;
    NodeDataType producedDataType = NodeDataType::None;
    std::vector<NodeGraphAttributeContract> requiredAttributes;
    std::vector<NodeGraphAttributeContract> guaranteedAttributes;
};

struct NodeSocketSignature {
    std::string name;
    NodeGraphSocketDirection direction = NodeGraphSocketDirection::Input;
    NodeGraphValueType valueType = NodeGraphValueType::Unknown;
    NodeGraphSocketContract contract;
};

enum class NodeGraphParamType {
    Float,
    Int,
    Bool,
    String
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
};

struct NodeGraphParamValue {
    uint32_t id = 0;
    NodeGraphParamType type = NodeGraphParamType::Float;
    double floatValue = 0.0;
    int64_t intValue = 0;
    bool boolValue = false;
    std::string stringValue;
};

namespace nodegraphparams {
namespace model {
constexpr uint32_t Path = 1;
constexpr uint32_t ApplyRequested = 2;
}

namespace remesh {
constexpr uint32_t Iterations = 1;
constexpr uint32_t MinAngleDegrees = 2;
constexpr uint32_t MaxEdgeLength = 3;
constexpr uint32_t StepSize = 4;
constexpr uint32_t RunRequested = 5;
}

namespace heatsolve {
constexpr uint32_t Enabled = 1;
constexpr uint32_t Paused = 2;
constexpr uint32_t ResetRequested = 3;
}
}

struct NodeTypeDefinition {
    NodeTypeId id;
    std::string displayName;
    NodeGraphNodeCategory category = NodeGraphNodeCategory::Custom;
    std::vector<NodeSocketSignature> sockets;
    std::vector<NodeGraphParamDefinition> parameters;
};

const std::vector<NodeTypeDefinition>& builtInNodeTypeDefinitions();
const NodeTypeDefinition* findNodeTypeDefinitionById(const NodeTypeId& typeId);
NodeTypeId canonicalNodeTypeId(const NodeTypeId& requestedTypeId);
const NodeGraphParamDefinition* findNodeParamDefinition(const NodeTypeDefinition& definition, uint32_t paramId);

struct NodeGraphSocket {
    NodeGraphSocketId id{};
    std::string name;
    NodeGraphValueType valueType = NodeGraphValueType::Unknown;
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

struct NodeGraphChange {
    NodeGraphChangeType type = NodeGraphChangeType::Reset;
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

NodeGraphParamValue makeNodeGraphParamValue(const NodeGraphParamDefinition& definition);
const NodeGraphParamValue* findNodeParamValue(const NodeGraphNode& node, uint32_t paramId);
NodeGraphParamValue* findNodeParamValue(NodeGraphNode& node, uint32_t paramId);
bool tryGetNodeParamFloat(const NodeGraphNode& node, uint32_t paramId, double& outValue);
bool tryGetNodeParamInt(const NodeGraphNode& node, uint32_t paramId, int64_t& outValue);
bool tryGetNodeParamBool(const NodeGraphNode& node, uint32_t paramId, bool& outValue);
bool tryGetNodeParamString(const NodeGraphNode& node, uint32_t paramId, std::string& outValue);
