#include "NodeGraphRegistry.hpp"
#include "NodeGraphParamUtils.hpp"

NodeSocketSignature NodeGraphRegistry::makeInputSocket(
    const char* name,
    NodeGraphValueType valueType) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Input;
    signature.valueType = valueType;
    return signature;
}

NodeSocketSignature NodeGraphRegistry::makeOutputSocket(
    const char* name,
    NodePayloadType producedPayloadType) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Output;
    signature.valueType = valueTypeOf(producedPayloadType);
    signature.contract.producedPayloadType = producedPayloadType;
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
                NodePayloadType::Geometry),
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
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket(
                "Mesh",
                NodePayloadType::Geometry),
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
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket(
                "Mesh",
                NodePayloadType::Geometry),
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
    return {
        nodegraphtypes::Remesh,
        "Remesh",
        NodeGraphNodeCategory::Meshing,
        {
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket("Mesh", NodePayloadType::Remesh),
        },
        {
            {nodegraphparams::remesh::Iterations, "Iterations", NodeGraphParamType::Int, 0.0, 1, false, "", false},
            {nodegraphparams::remesh::MinAngleDegrees, "Min Angle", NodeGraphParamType::Float, 20.0, 0, false, "", false},
            {nodegraphparams::remesh::MaxEdgeLength, "Max Edge Length", NodeGraphParamType::Float, 0.1, 0, false, "", false},
            {nodegraphparams::remesh::StepSize, "Step Size", NodeGraphParamType::Float, 0.25, 0, false, "", false},
            {nodegraphparams::remesh::RunRequested, "Run Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
            {nodegraphparams::remesh::ShowRemeshOverlay, "Show Remesh Overlay", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::remesh::ShowFaceNormals, "Show Face Normals", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::remesh::ShowVertexNormals, "Show Vertex Normals", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::remesh::NormalLength, "Normal Length", NodeGraphParamType::Float, 0.05, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildHeatReceiverNode() {
    return {
        nodegraphtypes::HeatReceiver,
        "Heat Receiver",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket(
                "Receiver",
                NodePayloadType::HeatReceiver),
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
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket(
                "Source",
                NodePayloadType::HeatSource),
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
            makeInputSocket("Emitter", NodeGraphValueType::Mesh),
            makeInputSocket("Receiver", NodeGraphValueType::Mesh),
            makeOutputSocket("Field", NodePayloadType::Contact),
        },
        {
            {nodegraphparams::contact::MinNormalDot, "Min Normal Dot", NodeGraphParamType::Float, -0.65, 0, false, "", false},
            {nodegraphparams::contact::ContactRadius, "Contact Radius", NodeGraphParamType::Float, 0.01, 0, false, "", false},
            {nodegraphparams::contact::ShowContactLines, "Show Contact Lines", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildVoronoiNode() {
    return {
        nodegraphtypes::Voronoi,
        "Voronoi",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket("Volume", NodePayloadType::Voronoi),
        },
        {
            {nodegraphparams::voronoi::CellSize, "Cell Size", NodeGraphParamType::Float, 0.005, 0, false, "", false},
            {nodegraphparams::voronoi::VoxelResolution, "Voxel Resolution", NodeGraphParamType::Int, 0.0, 128, false, "", false},
            {nodegraphparams::voronoi::ShowVoronoi, "Show Voronoi", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::voronoi::ShowPoints, "Show Points", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildHeatSolveNode() {
    NodeGraphParamDefinition materialBindingDefinition = makeStructParamDefinition(
        0,
        "Material Binding",
        {
            makeParamField("receiverModelNodeId", makeIntParamDefinition(0, "Receiver Model Node ID")),
            makeParamField(
                "preset",
                makeEnumParamDefinition(
                    0,
                    "Preset",
                    "Aluminum",
                    {"Aluminum", "Copper", "Custom", "Iron", "Ceramic"})),
        });

    return {
        nodegraphtypes::HeatSolve,
        "Heat Solve",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Volume", NodeGraphValueType::Volume),
            makeInputSocket("Field", NodeGraphValueType::Field),
            makeOutputSocket("Heat", NodePayloadType::Heat),
        },
        {
            {nodegraphparams::heatsolve::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::Paused, "Paused", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::ResetRequested, "Reset Requested", NodeGraphParamType::Bool, 0.0, 0, false, "", true},
            makeArrayParamDefinition(nodegraphparams::heatsolve::MaterialBindings, "Material Bindings", std::move(materialBindingDefinition)),
            {nodegraphparams::heatsolve::CellSize, "Cell Size", NodeGraphParamType::Float, 0.005, 0, false, "", false},
            {nodegraphparams::heatsolve::VoxelResolution, "Voxel Resolution", NodeGraphParamType::Int, 0.0, 128, false, "", false},
            {nodegraphparams::heatsolve::ShowHeatOverlay, "Show Heat Overlay", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::ContactThermalConductance, "Contact Thermal Conductance", NodeGraphParamType::Float, 16000.0, 0, false, "", false},
        },
    };
}

NodeTypeDefinition NodeGraphRegistry::buildCustomNode() {
    return {
        nodegraphtypes::Custom,
        "Custom",
        NodeGraphNodeCategory::Custom,
        {
            makeInputSocket("In", NodeGraphValueType::None),
            makeOutputSocket("Out", NodePayloadType::None),
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
