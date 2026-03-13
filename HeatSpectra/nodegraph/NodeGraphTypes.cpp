#include "NodeGraphTypes.hpp"

#include <utility>

namespace {

NodeGraphAttributeContract makeAttributeContract(
    const char* name,
    GeometryAttributeDomain domain,
    GeometryAttributeDataType dataType = GeometryAttributeDataType::Float,
    uint32_t tupleSize = 1) {
    NodeGraphAttributeContract contract{};
    contract.name = name;
    contract.domain = domain;
    contract.dataType = dataType;
    contract.tupleSize = tupleSize;
    return contract;
}

NodeSocketSignature makeInputSocket(
    const char* name,
    NodeGraphValueType valueType,
    std::vector<NodeDataType> acceptedDataTypes,
    std::vector<NodeGraphAttributeContract> requiredAttributes = {}) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Input;
    signature.valueType = valueType;
    signature.contract.acceptedDataTypes = std::move(acceptedDataTypes);
    signature.contract.requiredAttributes = std::move(requiredAttributes);
    return signature;
}

NodeSocketSignature makeOutputSocket(
    const char* name,
    NodeGraphValueType valueType,
    NodeDataType producedDataType,
    std::vector<NodeGraphAttributeContract> guaranteedAttributes = {}) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Output;
    signature.valueType = valueType;
    signature.contract.producedDataType = producedDataType;
    signature.contract.guaranteedAttributes = std::move(guaranteedAttributes);
    return signature;
}

const std::vector<NodeTypeDefinition>& builtInNodeTypeDefinitionsStorage() {
    static const std::vector<NodeTypeDefinition> definitions = {
        {
            nodegraphtypes::Model,
            "Model",
            NodeGraphNodeCategory::Model,
            {
                makeOutputSocket(
                    "Mesh",
                    NodeGraphValueType::Mesh,
                    NodeDataType::Geometry,
                    {
                        makeAttributeContract(
                            "P",
                            GeometryAttributeDomain::Point,
                            GeometryAttributeDataType::Float,
                            3),
                        makeAttributeContract(
                            "group.id",
                            GeometryAttributeDomain::Primitive,
                            GeometryAttributeDataType::Int),
                    }),
            },
            {
                {nodegraphparams::model::Path, "Model Path", NodeGraphParamType::String, 0.0, 0, false, "", false},
                {nodegraphparams::model::ApplyRequested, "Apply Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
            },
        },
        {
            nodegraphtypes::Group,
            "Group",
            NodeGraphNodeCategory::Meshing,
            {
                makeInputSocket("Mesh", NodeGraphValueType::Mesh, {NodeDataType::Geometry}),
                makeOutputSocket(
                    "Mesh",
                    NodeGraphValueType::Mesh,
                    NodeDataType::Geometry,
                    {
                        makeAttributeContract(
                            "P",
                            GeometryAttributeDomain::Point,
                            GeometryAttributeDataType::Float,
                            3),
                        makeAttributeContract(
                            "group.id",
                            GeometryAttributeDomain::Primitive,
                            GeometryAttributeDataType::Int),
                    }),
            },
            {
                {nodegraphparams::group::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, true, "", false},
                {nodegraphparams::group::SourceType, "Source Type", NodeGraphParamType::Int, 0.0, nodegraphparams::group::sourcetype::Vertex, false, "", false},
                {nodegraphparams::group::SourceName, "Source Name", NodeGraphParamType::String, 0.0, 0, false, "", false},
                {nodegraphparams::group::TargetName, "Target Group Name", NodeGraphParamType::String, 0.0, 0, false, "", false},
            },
        },
        {
            nodegraphtypes::Remesh,
            "Remesh",
            NodeGraphNodeCategory::Meshing,
            {
                makeInputSocket("Mesh", NodeGraphValueType::Mesh, {NodeDataType::Geometry}),
                makeOutputSocket(
                    "Mesh",
                    NodeGraphValueType::Mesh,
                    NodeDataType::Geometry,
                    {
                        makeAttributeContract(
                            "P",
                            GeometryAttributeDomain::Point,
                            GeometryAttributeDataType::Float,
                            3),
                        makeAttributeContract(
                            "group.id",
                            GeometryAttributeDomain::Primitive,
                            GeometryAttributeDataType::Int),
                    }),
            },
            {
                {nodegraphparams::remesh::Iterations, "Iterations", NodeGraphParamType::Int, 0.0, 1, false, "", false},
                {nodegraphparams::remesh::MinAngleDegrees, "Min Angle", NodeGraphParamType::Float, 30.0, 0, false, "", false},
                {nodegraphparams::remesh::MaxEdgeLength, "Max Edge Length", NodeGraphParamType::Float, 0.1, 0, false, "", false},
                {nodegraphparams::remesh::StepSize, "Step Size", NodeGraphParamType::Float, 0.25, 0, false, "", false},
                {nodegraphparams::remesh::RunRequested, "Run Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
            },
        },
        {
            nodegraphtypes::HeatReceiver,
            "Heat Receiver",
            NodeGraphNodeCategory::System,
            {
                makeInputSocket("Model", NodeGraphValueType::Mesh, {NodeDataType::Geometry}),
                makeOutputSocket(
                    "Receiver",
                    NodeGraphValueType::HeatReceiver,
                    NodeDataType::HeatReceiver,
                    {
                        makeAttributeContract(
                            "receiver.active",
                            GeometryAttributeDomain::Detail,
                            GeometryAttributeDataType::Bool),
                    }),
            },
            {},
        },
        {
            nodegraphtypes::HeatSource,
            "Heat Source",
            NodeGraphNodeCategory::System,
            {
                makeInputSocket("Model", NodeGraphValueType::Mesh, {NodeDataType::Geometry}),
                makeOutputSocket(
                    "Source",
                    NodeGraphValueType::HeatSource,
                    NodeDataType::HeatSource,
                    {
                        makeAttributeContract(
                            "source.active",
                            GeometryAttributeDomain::Detail,
                            GeometryAttributeDataType::Bool),
                        makeAttributeContract(
                            "temperature",
                            GeometryAttributeDomain::Point,
                            GeometryAttributeDataType::Float),
                    }),
            },
            {},
        },
        {
            nodegraphtypes::ContactPair,
            "Contact Pair",
            NodeGraphNodeCategory::System,
            {
                makeInputSocket(
                    "Emitter",
                    NodeGraphValueType::Unknown,
                    {NodeDataType::HeatReceiver, NodeDataType::HeatSource}),
                makeInputSocket(
                    "Receiver",
                    NodeGraphValueType::HeatReceiver,
                    {NodeDataType::HeatReceiver}),
                makeOutputSocket(
                    "Contact Pair",
                    NodeGraphValueType::ContactPair,
                    NodeDataType::ContactPair),
            },
            {
                {nodegraphparams::contactpair::MinNormalDot, "Min Normal Dot", NodeGraphParamType::Float, -0.65, 0, false, "", false},
                {nodegraphparams::contactpair::ContactRadius, "Contact Radius", NodeGraphParamType::Float, 0.01, 0, false, "", false},
                {nodegraphparams::contactpair::ComputeRequested, "Compute Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
            },
        },
        {
            nodegraphtypes::HeatSolve,
            "Heat Solve",
            NodeGraphNodeCategory::System,
            {
                makeInputSocket(
                    "Contact Pair",
                    NodeGraphValueType::ContactPair,
                    {NodeDataType::ContactPair}),
                makeInputSocket(
                    "Group",
                    NodeGraphValueType::Mesh,
                    {NodeDataType::Geometry}),
            },
            {
                {nodegraphparams::heatsolve::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
                {nodegraphparams::heatsolve::Paused, "Paused", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
                {nodegraphparams::heatsolve::ResetRequested, "Reset Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
                {nodegraphparams::heatsolve::MaterialBindings, "Material Bindings", NodeGraphParamType::String, 0.0, 0, false, "", false},
                {nodegraphparams::heatsolve::ContactBindings, "Contact Bindings", NodeGraphParamType::String, 0.0, 0, false, "", false},
                {nodegraphparams::heatsolve::CellSize, "Cell Size", NodeGraphParamType::Float, 0.005, 0, false, "", false},
                {nodegraphparams::heatsolve::VoxelResolution, "Voxel Resolution", NodeGraphParamType::Int, 0.0, 128, false, "", false},
            },
        },
        {
            nodegraphtypes::Custom,
            "Custom",
            NodeGraphNodeCategory::Custom,
            {
                makeInputSocket("In", NodeGraphValueType::Unknown, {}),
                makeOutputSocket("Out", NodeGraphValueType::Unknown, NodeDataType::None),
            },
            {},
        },
    };

    return definitions;
}

}

const std::vector<NodeTypeDefinition>& builtInNodeTypeDefinitions() {
    return builtInNodeTypeDefinitionsStorage();
}

const NodeTypeDefinition* findNodeTypeDefinitionById(const NodeTypeId& typeId) {
    const std::vector<NodeTypeDefinition>& definitions = builtInNodeTypeDefinitionsStorage();
    for (const NodeTypeDefinition& definition : definitions) {
        if (definition.id == typeId) {
            return &definition;
        }
    }

    return nullptr;
}

NodeTypeId canonicalNodeTypeId(const NodeTypeId& requestedTypeId) {
    if (const NodeTypeDefinition* definition = findNodeTypeDefinitionById(requestedTypeId)) {
        return definition->id;
    }

    return nodegraphtypes::Custom;
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
