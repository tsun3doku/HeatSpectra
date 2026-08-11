#include "NodeGraphInit.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphTypes.hpp"
#include "NodeGraphParamUtils.hpp"
#include "heat/HeatGpuStructs.hpp"
#include "heat/HeatSystemPresets.hpp"

#include <cstdint>
#include <initializer_list>

namespace payloadtypes {
    uint8_t None = 0;
    uint8_t Geometry = 0;
    uint8_t Remesh = 0;
    uint8_t HeatModel = 0;
    uint8_t Heat = 0;
    uint8_t Voronoi = 0;
    uint8_t Contact = 0;
    uint8_t Points = 0;
    uint8_t SerialTemperature = 0;
}

static NodeSocketSignature makeInputSocket(
    const char* name,
    NodeGraphValueType valueType,
    bool variadic = false,
    bool required = true) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Input;
    signature.valueType = valueType;
    signature.variadic = variadic;
    signature.required = required;
    return signature;
}

static NodeSocketSignature makeOutputSocket(
    const char* name,
    NodeGraphValueType valueType,
    uint8_t producedPayloadType) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Output;
    signature.valueType = valueType;
    signature.contract.producedPayloadType = producedPayloadType;
    return signature;
}

static NodeSocketSignature makePolymorphicInputSocket(
    const char* name,
    std::initializer_list<NodeGraphValueType> acceptedValueTypes,
    bool variadic = false,
    bool required = true) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Input;
    signature.valueType = NodeGraphValueType::None;
    signature.acceptedValueTypes = acceptedValueTypes;
    signature.variadic = variadic;
    signature.required = required;
    return signature;
}

static NodeSocketSignature makePolymorphicOutputSocket(
    const char* name,
    std::initializer_list<NodeGraphValueType> acceptedValueTypes,
    uint8_t producedPayloadType) {
    NodeSocketSignature signature{};
    signature.name = name;
    signature.direction = NodeGraphSocketDirection::Output;
    signature.valueType = NodeGraphValueType::None;
    signature.acceptedValueTypes = acceptedValueTypes;
    signature.contract.producedPayloadType = producedPayloadType;
    return signature;
}

static NodeTypeDefinition buildModelNode() {
    return {
        nodegraphtypes::Model,
        "Model",
        NodeGraphNodeCategory::Geometry,
        {
            makeOutputSocket("Mesh", NodeGraphValueType::Mesh, payloadtypes::Geometry),
        },
        {
            {nodegraphparams::model::Path, "Model Path", NodeGraphParamType::String, 0.0, 0, false, "", false},
            makeEnumParamDefinition(nodegraphparams::model::SourceUnit, "Import Units", "Meters", {"Meters", "Centimeters", "Millimeters"}),
        },
    };
}

static NodeTypeDefinition buildTransformNode() {
    return {
        nodegraphtypes::Transform,
        "Transform",
        NodeGraphNodeCategory::Meshing,
        {
            makePolymorphicInputSocket("Geometry", {NodeGraphValueType::Mesh, NodeGraphValueType::Points}, false, false),
            makePolymorphicOutputSocket("Geometry", {NodeGraphValueType::Mesh, NodeGraphValueType::Points}, payloadtypes::None),
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

static NodeTypeDefinition buildGroupNode() {
    return {
        nodegraphtypes::Group,
        "Group",
        NodeGraphNodeCategory::Meshing,
        {
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket("Mesh", NodeGraphValueType::Mesh, payloadtypes::Geometry),
        },
        {
            {nodegraphparams::group::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, true, "", false},
            {nodegraphparams::group::SourceType, "Source Type", NodeGraphParamType::Int, 0.0, nodegraphparams::group::sourcetype::Vertex, false, "", false},
            {nodegraphparams::group::SourceName, "Source Name", NodeGraphParamType::String, 0.0, 0, false, "", false},
            {nodegraphparams::group::TargetName, "Target Group Name", NodeGraphParamType::String, 0.0, 0, false, "", false},
        },
    };
}

static NodeTypeDefinition buildRemeshNode() {
    return {
        nodegraphtypes::Remesh,
        "Remesh",
        NodeGraphNodeCategory::Meshing,
        {
            makeInputSocket("Mesh", NodeGraphValueType::Mesh),
            makeOutputSocket("Remesh", NodeGraphValueType::Remesh, payloadtypes::Remesh),
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

static NodeTypeDefinition buildHeatModelNode() {
    return {
        nodegraphtypes::HeatModel,
        "Heat Model",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Remesh", NodeGraphValueType::Remesh),
            makeInputSocket("Robin Temperature (C)", NodeGraphValueType::ScalarFloat, false, false),
            makeOutputSocket("HeatModel", NodeGraphValueType::HeatModel, payloadtypes::HeatModel),
        },
        {
            makeEnumParamDefinition(
                nodegraphparams::heatmodel::MaterialPreset,
                "Material Preset",
                "Custom",
                {"Aluminum", "Copper", "Custom", "Iron", "Ceramic"}),
            {nodegraphparams::heatmodel::Density, "Density", NodeGraphParamType::Float, HeatSimDefaults::density, 0, false, "", false},
            {nodegraphparams::heatmodel::SpecificHeat, "Specific Heat", NodeGraphParamType::Float, HeatSimDefaults::specificHeat, 0, false, "", false},
            {nodegraphparams::heatmodel::Conductivity, "Conductivity", NodeGraphParamType::Float, HeatSimDefaults::conductivity, 0, false, "", false},
            {nodegraphparams::heatmodel::InitialTemperatureC, "Initial Temperature (C)", NodeGraphParamType::Float, HeatSimDefaults::ambientTemperatureC, 0, false, "", false},
            makeEnumParamDefinition(
                nodegraphparams::heatmodel::BoundaryCondition,
                "Boundary Condition",
                "Adiabatic",
                {"Adiabatic", "Dirichlet Temperature", "Neumann Heat Flux", "Robin Convection"}),
            {nodegraphparams::heatmodel::DirichletTemperatureC, "Boundary Temperature (C)", NodeGraphParamType::Float, HeatSimDefaults::ambientTemperatureC, 0, false, "", false},
            {nodegraphparams::heatmodel::HeatFlux, "Inward Heat Flux (W/m²)", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::heatmodel::HeatTransferCoefficient, "Heat Transfer Coefficient (W/(m²·K))", NodeGraphParamType::Float, 0.0, 0, false, "", false},
            {nodegraphparams::heatmodel::VolumetricPowerDensity, "Volumetric Power Density (W/m³)", NodeGraphParamType::Float, 0.0, 0, false, "", false},
        },
    };
}

static NodeTypeDefinition buildSerialTemperatureNode() {
    return {
        nodegraphtypes::SerialTemperature,
        "Serial Temperature",
        NodeGraphNodeCategory::System,
        {
            makeOutputSocket(
                "Temperature", NodeGraphValueType::ScalarFloat, payloadtypes::SerialTemperature),
        },
        {
            {nodegraphparams::serialtemperature::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, true, "", false},
            {nodegraphparams::serialtemperature::Port, "Port", NodeGraphParamType::String, 0.0, 0, false, "", false},
            {nodegraphparams::serialtemperature::BaudRate, "Baud Rate", NodeGraphParamType::Int, 0.0, 115200, false, "", false},
        },
    };
}

static NodeTypeDefinition buildContactNode() {
    return {
        nodegraphtypes::Contact,
        "Contact",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("SurfaceA", NodeGraphValueType::Remesh),
            makeInputSocket("SurfaceB", NodeGraphValueType::Remesh),
            makeOutputSocket("Field", NodeGraphValueType::Field, payloadtypes::Contact),
        },
        {
            {nodegraphparams::contact::MinNormalDot, "Min Normal Dot", NodeGraphParamType::Float, HeatSimDefaults::minNormalDot, 0, false, "", false},
            {nodegraphparams::contact::ContactRadius, "Contact Radius", NodeGraphParamType::Float, HeatSimDefaults::contactRadius, 0, false, "", false},
            {nodegraphparams::contact::ShowContactLines, "Show Contact Lines", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
        },
    };
}

static NodeTypeDefinition buildVoronoiNode() {
    return {
        nodegraphtypes::Voronoi,
        "Voronoi",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Remesh", NodeGraphValueType::Remesh, false, false),
            makeInputSocket("Points", NodeGraphValueType::Points, false, true),
            makeOutputSocket("Volume", NodeGraphValueType::Volume, payloadtypes::Voronoi),
        },
        {
            {nodegraphparams::voronoi::SDFSize, "SDF Size", NodeGraphParamType::Float, 0.005, 0, false, "", false},
            {nodegraphparams::voronoi::VoxelResolution, "Voxel Resolution", NodeGraphParamType::Int, 0.0, 128, false, "", false},
            {nodegraphparams::voronoi::ShowVoronoi, "Show Voronoi", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::voronoi::ShowPoints, "Show Points", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
        },
    };
}

static NodeTypeDefinition buildHeatSolveNode() {
    return {
        nodegraphtypes::HeatSolve,
        "Ember",
        NodeGraphNodeCategory::System,
        {
            makeInputSocket("Volume", NodeGraphValueType::Volume, true),
            makeInputSocket("Field", NodeGraphValueType::Field, true),
            makeInputSocket("HeatModel", NodeGraphValueType::HeatModel, true, false),
            makeOutputSocket("Heat", NodeGraphValueType::None, payloadtypes::Heat),
        },
        {
            {nodegraphparams::heatsolve::Enabled, "Enabled", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::Paused, "Paused", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::ResetRequested, "Reset Requested", NodeGraphParamType::Int, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::ShowHeatOverlay, "Show Heat Overlay", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::ContactThermalConductance, "Contact Thermal Conductance", NodeGraphParamType::Float, HeatSimDefaults::contactThermalConductance, 0, false, "", false},
            {nodegraphparams::heatsolve::ShowFluxVectors, "Show Flux Vectors", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::ShowHeatPalette, "Show Heat Palette", NodeGraphParamType::Bool, 0.0, 0, false, "", false},
            {nodegraphparams::heatsolve::FluxVectorScale, "Flux Vector Scale", NodeGraphParamType::Float, 1.0, 0, false, "", false},
            {nodegraphparams::heatsolve::RewindFrame, "Rewind Frame", NodeGraphParamType::Int, 0.0, static_cast<int64_t>(heat::NoRewindFrame), false, "", false},
            {nodegraphparams::heatsolve::SimulationDuration, "Simulation Duration", NodeGraphParamType::Float, 5.0, 0, false, "", false},
        },
    };
}

static NodeTypeDefinition buildPointsNode() {
    return {
        nodegraphtypes::Points,
        "Points",
        NodeGraphNodeCategory::Geometry,
        {
            makeOutputSocket("Points", NodeGraphValueType::Points, payloadtypes::Points),
        },
        {
            {nodegraphparams::points::PointCount, "Point Count", NodeGraphParamType::Int, 0.0, 1000, false, "", false},
            {nodegraphparams::points::DimX, "Dim X", NodeGraphParamType::Float, 1.0, 0, false, "", false},
            {nodegraphparams::points::DimY, "Dim Y", NodeGraphParamType::Float, 1.0, 0, false, "", false},
            {nodegraphparams::points::DimZ, "Dim Z", NodeGraphParamType::Float, 1.0, 0, false, "", false},
        },
    };
}

static NodeTypeDefinition buildMeshPointsNode() {
    return {
        nodegraphtypes::MeshPoints,
        "Mesh Points",
        NodeGraphNodeCategory::Geometry,
        {
            makePolymorphicInputSocket("Geometry", {NodeGraphValueType::Mesh, NodeGraphValueType::Remesh}),
            makeOutputSocket("Points", NodeGraphValueType::Points, payloadtypes::Points),
        },
        {},
    };
}

static NodeTypeDefinition buildMergeNode() {
    return {
        nodegraphtypes::Merge,
        "Merge",
        NodeGraphNodeCategory::Meshing,
        {
            makePolymorphicInputSocket("Geometry", {NodeGraphValueType::Mesh, NodeGraphValueType::Points}, true),
            makePolymorphicOutputSocket("Geometry", {NodeGraphValueType::Mesh, NodeGraphValueType::Points}, payloadtypes::None),
        },
        {},
    };
}

void initNodeGraph(NodeGraphRegistry& registry) {
    payloadtypes::Geometry   = registry.registerPayloadType("geometry", NodeGraphValueType::Mesh);
    payloadtypes::Remesh     = registry.registerPayloadType("remesh",   NodeGraphValueType::Remesh);
    payloadtypes::HeatModel  = registry.registerPayloadType("heat_model", NodeGraphValueType::HeatModel);
    payloadtypes::Heat       = registry.registerPayloadType("heat",      NodeGraphValueType::None);
    payloadtypes::Voronoi    = registry.registerPayloadType("voronoi",   NodeGraphValueType::Volume);
    payloadtypes::Contact    = registry.registerPayloadType("contact",   NodeGraphValueType::Field);
    payloadtypes::Points     = registry.registerPayloadType("points",    NodeGraphValueType::Points);
    payloadtypes::SerialTemperature = registry.registerPayloadType("serial_temperature", NodeGraphValueType::ScalarFloat);

    registry.registerNodeType(buildModelNode());
    registry.registerNodeType(buildTransformNode());
    registry.registerNodeType(buildGroupNode());
    registry.registerNodeType(buildRemeshNode());
    registry.registerNodeType(buildHeatModelNode());
    registry.registerNodeType(buildContactNode());
    registry.registerNodeType(buildVoronoiNode());
    registry.registerNodeType(buildHeatSolveNode());
    registry.registerNodeType(buildPointsNode());
    registry.registerNodeType(buildMeshPointsNode());
    registry.registerNodeType(buildMergeNode());
    registry.registerNodeType(buildSerialTemperatureNode());
}
