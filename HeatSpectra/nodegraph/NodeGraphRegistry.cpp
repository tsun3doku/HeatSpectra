#include "NodeGraphRegistry.hpp"

#include "domain/RemeshParams.hpp"

#include <utility>

NodeGraphAttributeContract NodeGraphRegistry::makeAttributeContract(
    const char* name,
    GeometryAttributeDomain domain,
    GeometryAttributeDataType dataType,
    uint32_t tupleSize) {
    NodeGraphAttributeContract contract{};
    contract.name = name;
    contract.domain = domain;
    contract.dataType = dataType;
    contract.tupleSize = tupleSize;
    return contract;
}

NodeSocketSignature NodeGraphRegistry::makeInputSocket(
    const char* name,
    NodeGraphValueType valueType,
    std::vector<NodeDataType> acceptedDataTypes,
    std::vector<NodeGraphAttributeContract> requiredAttributes) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Input;
    signature.valueType = valueType;
    signature.contract.acceptedDataTypes = std::move(acceptedDataTypes);
    signature.contract.requiredAttributes = std::move(requiredAttributes);
    return signature;
}

NodeSocketSignature NodeGraphRegistry::makeOutputSocket(
    const char* name,
    NodeGraphValueType valueType,
    NodeDataType producedDataType,
    std::vector<NodeGraphAttributeContract> guaranteedAttributes) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Output;
    signature.valueType = valueType;
    signature.contract.producedDataType = producedDataType;
    signature.contract.guaranteedAttributes = std::move(guaranteedAttributes);
    return signature;
}

NodeTypeDefinition NodeGraphRegistry::buildModelNode() {
    return {
        nodegraphtypes::Model,
        "Model",
        NodeGraphNodeCategory::Model,
        {
            makeOutputSocket(
                "Mesh",
                NodeGraphValueType::Mesh,
                NodeDataType::Geometry,
                {
                    makeAttributeContract("P", GeometryAttributeDomain::Point, GeometryAttributeDataType::Float, 3),
                    makeAttributeContract("group.id", GeometryAttributeDomain::Primitive, GeometryAttributeDataType::Int),
                }),
        },
        {
            {nodegraphparams::model::Path, "Model Path", NodeGraphParamType::String, 0.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildTransformNode() {
    return {
        nodegraphtypes::Transform,
        "Transform",
        NodeGraphNodeCategory::Meshing,
        {
            makeInputSocket("Mesh", NodeGraphValueType::Mesh, {NodeDataType::Geometry}),
            makeOutputSocket(
                "Mesh",
                NodeGraphValueType::Mesh,
                NodeDataType::Geometry,
                {
                    makeAttributeContract("P", GeometryAttributeDomain::Point, GeometryAttributeDataType::Float, 3),
                    makeAttributeContract("group.id", GeometryAttributeDomain::Primitive, GeometryAttributeDataType::Int),
                }),
        },
        {
            {nodegraphparams::transform::TranslateX, "Translate X", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::transform::TranslateY, "Translate Y", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::transform::TranslateZ, "Translate Z", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::transform::RotateXDegrees, "Rotate X", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::transform::RotateYDegrees, "Rotate Y", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::transform::RotateZDegrees, "Rotate Z", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::transform::ScaleX, "Scale X", NodeGraphParamType::Float, 1.0, 0, false, "", false},
            {nodegraphparams::transform::ScaleY, "Scale Y", NodeGraphParamType::Float, 1.0, 0, false, "", false},
            {nodegraphparams::transform::ScaleZ, "Scale Z", NodeGraphParamType::Float, 1.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildGroupNode() {
    return {
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
                    makeAttributeContract("P", GeometryAttributeDomain::Point, GeometryAttributeDataType::Float, 3),
                    makeAttributeContract("group.id", GeometryAttributeDomain::Primitive, GeometryAttributeDataType::Int),
                }),
        },
        {
            {nodegraphparams::group::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, true, "", false},
            {nodegraphparams::group::SourceType, "Source Type", NodeGraphParamType::Int, 0.0, nodegraphparams::group::sourcetype::Vertex, false, "", false},
            {nodegraphparams::group::SourceName, "Source Name", NodeGraphParamType::String, 0.0, 0, false, "", false},
            {nodegraphparams::group::TargetName, "Target Group Name", NodeGraphParamType::String, 0.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildRemeshNode() {
    const RemeshParams defaults{};

    return {
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
                    makeAttributeContract("P", GeometryAttributeDomain::Point, GeometryAttributeDataType::Float, 3),
                    makeAttributeContract("group.id", GeometryAttributeDomain::Primitive, GeometryAttributeDataType::Int),
                }),
            makeOutputSocket("Intrinsic", NodeGraphValueType::Intrinsic, NodeDataType::Intrinsic),
        },
        {
            {nodegraphparams::remesh::Iterations, "Iterations", NodeGraphParamType::Int, 0.0, defaults.iterations, false, "", false},
            {nodegraphparams::remesh::MinAngleDegrees, "Min Angle", NodeGraphParamType::Float, defaults.minAngleDegrees, 0, false, "", false},
            {nodegraphparams::remesh::MaxEdgeLength, "Max Edge Length", NodeGraphParamType::Float, defaults.maxEdgeLength, 0, false, "", false},
            {nodegraphparams::remesh::StepSize, "Step Size", NodeGraphParamType::Float, defaults.stepSize, 0, false, "", false},
            {nodegraphparams::remesh::RunRequested, "Run Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildHeatReceiverNode() {
    return {
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
                    makeAttributeContract("receiver.active", GeometryAttributeDomain::Detail, GeometryAttributeDataType::Bool),
                }),
        },
        {},
    };
}

NodeTypeDefinition NodeGraphRegistry::buildHeatSourceNode() {
    return {
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
                    makeAttributeContract("source.active", GeometryAttributeDomain::Detail, GeometryAttributeDataType::Bool),
                    makeAttributeContract("temperature", GeometryAttributeDomain::Point, GeometryAttributeDataType::Float),
                }),
        },
        {
            {nodegraphparams::heatsource::Temperature, "Temperature", NodeGraphParamType::Float, 100.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildContactNode() {
    return {
        nodegraphtypes::Contact,
        "Contact",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Emitter", NodeGraphValueType::Unknown, {NodeDataType::HeatReceiver, NodeDataType::HeatSource}),
            makeInputSocket("Receiver", NodeGraphValueType::HeatReceiver, {NodeDataType::HeatReceiver}),
            makeOutputSocket("Contact", NodeGraphValueType::Contact, NodeDataType::Contact),
        },
        {
            {nodegraphparams::contact::MinNormalDot, "Min Normal Dot", NodeGraphParamType::Float, -0.65, 0, false, "", false},
            {nodegraphparams::contact::ContactRadius, "Contact Radius", NodeGraphParamType::Float, 0.01, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildVoronoiNode() {
    return {
        nodegraphtypes::Voronoi,
        "Voronoi",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Geometry", NodeGraphValueType::Mesh, {NodeDataType::Geometry}),
            makeOutputSocket("Voronoi", NodeGraphValueType::Voronoi, NodeDataType::Voronoi),
        },
        {
            {nodegraphparams::voronoi::CellSize, "Cell Size", NodeGraphParamType::Float, 0.005, 0, false, "", false},
            {nodegraphparams::voronoi::VoxelResolution, "Voxel Resolution", NodeGraphParamType::Int, 0.0, 128, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildHeatSolveNode() {
    return {
        nodegraphtypes::HeatSolve,
        "Heat Solve",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Voronoi", NodeGraphValueType::Voronoi, {NodeDataType::Voronoi}),
            makeInputSocket("Contact", NodeGraphValueType::Contact, {NodeDataType::Contact}),
            makeOutputSocket("Heat", NodeGraphValueType::Heat, NodeDataType::Heat),
        },
        {
            {nodegraphparams::heatsolve::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::Paused, "Paused", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::ResetRequested, "Reset Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
            {nodegraphparams::heatsolve::MaterialBindings, "Material Bindings", NodeGraphParamType::String, 0.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildCustomNode() {
    return {
        nodegraphtypes::Custom,
        "Custom",
        NodeGraphNodeCategory::Custom,
        {
            makeInputSocket("In", NodeGraphValueType::Unknown, {}),
            makeOutputSocket("Out", NodeGraphValueType::Unknown, NodeDataType::None),
        },
        {},
    };
}

const std::vector<NodeTypeDefinition>& NodeGraphRegistry::getBuiltInNodes() {
    static const std::vector<NodeTypeDefinition> definitions = {
        buildModelNode(),
        buildTransformNode(),
        buildGroupNode(),
        buildRemeshNode(),
        buildHeatReceiverNode(),
        buildHeatSourceNode(),
        buildContactNode(),
        buildVoronoiNode(),
        buildHeatSolveNode(),
        buildCustomNode(),
    };
    return definitions;
}

const NodeTypeDefinition* NodeGraphRegistry::findNodeById(const NodeTypeId& typeId) {
    const std::vector<NodeTypeDefinition>& definitions = getBuiltInNodes();
    for (const NodeTypeDefinition& definition : definitions) {
        if (definition.id == typeId) {
            return &definition;
        }
    }
    return nullptr;
}
