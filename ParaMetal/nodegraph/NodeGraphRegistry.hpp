#pragma once

#include "NodeGraphTypes.hpp"
#include "NodeGraphTypeRegistry.hpp"

#include <string>
#include <vector>
#include <unordered_map>

namespace nodegraphtypes {
inline constexpr const char* Model = "model";
inline constexpr const char* Transform = "transform";
inline constexpr const char* Group = "group";
inline constexpr const char* Remesh = "remesh";
inline constexpr const char* HeatModel = "heat_model";
inline constexpr const char* Contact = "contact";
inline constexpr const char* Voronoi = "voronoi";
inline constexpr const char* HeatSolve = "heat_solve";
inline constexpr const char* Points = "points";
inline constexpr const char* HeatPoints = "heat_points";
inline constexpr const char* MeshPoints = "mesh_points";
inline constexpr const char* Merge = "merge";
inline constexpr const char* SerialTemperature = "serial_temperature";
}

namespace nodegraphparams {
namespace model {
constexpr uint32_t Path = 1;
constexpr uint32_t SourceUnit = 2;
}

namespace transform {
constexpr uint32_t TranslateX = 1;
constexpr uint32_t TranslateY = 2;
constexpr uint32_t TranslateZ = 3;
constexpr uint32_t RotateXDegrees = 4;
constexpr uint32_t RotateYDegrees = 5;
constexpr uint32_t RotateZDegrees = 6;
constexpr uint32_t ScaleX = 7;
constexpr uint32_t ScaleY = 8;
constexpr uint32_t ScaleZ = 9;
}

namespace group {
constexpr uint32_t Enabled = 1;
constexpr uint32_t SourceName = 2;
constexpr uint32_t TargetName = 3;
constexpr uint32_t SourceType = 4;

namespace sourcetype {
constexpr int64_t Vertex = 0;
constexpr int64_t Object = 1;
constexpr int64_t Material = 2;
constexpr int64_t Smooth = 3;
}
}

namespace remesh {
constexpr uint32_t Iterations = 1;
constexpr uint32_t MinAngleDegrees = 2;
constexpr uint32_t MaxEdgeLength = 3;
constexpr uint32_t StepSize = 4;
constexpr uint32_t RunRequested = 5;
constexpr uint32_t ShowRemeshOverlay = 6;
constexpr uint32_t ShowFaceNormals = 7;
constexpr uint32_t ShowVertexNormals = 8;
constexpr uint32_t NormalLength = 9;
}

namespace heatsolve {
constexpr uint32_t Enabled = 1;
constexpr uint32_t Paused = 2;
constexpr uint32_t ResetRequested = 3;
constexpr uint32_t ShowHeatOverlay = 5;
constexpr uint32_t ContactThermalConductance = 6;
constexpr uint32_t ShowFluxVectors = 7;
constexpr uint32_t FluxVectorScale = 8;
constexpr uint32_t ShowHeatPalette = 9;
constexpr uint32_t RewindFrame = 10;
constexpr uint32_t SimulationDuration = 11;
}

namespace voronoi {
constexpr uint32_t SDFSize = 1;
constexpr uint32_t VoxelResolution = 2;
constexpr uint32_t ShowVoronoi = 3;
constexpr uint32_t ShowPoints = 4;
}

namespace contact {
constexpr uint32_t MinNormalDot = 1;
constexpr uint32_t ContactRadius = 2;
constexpr uint32_t ShowContactLines = 3;
}

namespace heatsource {
constexpr uint32_t Temperature = 1;
}

namespace heatmodel {
constexpr uint32_t Density = 1;
constexpr uint32_t SpecificHeat = 2;
constexpr uint32_t Conductivity = 3;
constexpr uint32_t InitialTemperatureC = 4;
constexpr uint32_t BoundaryCondition = 5;
constexpr uint32_t DirichletTemperatureC = 6;
constexpr uint32_t MaterialPreset = 7;
constexpr uint32_t HeatFlux = 8;
constexpr uint32_t HeatTransferCoefficient = 9;
constexpr uint32_t VolumetricPowerDensity = 10;
}

namespace serialtemperature {
constexpr uint32_t Enabled = 1;
constexpr uint32_t Port = 2;
constexpr uint32_t BaudRate = 3;
}

namespace points {
constexpr uint32_t PointCount = 1;
constexpr uint32_t DimX = 2;
constexpr uint32_t DimY = 3;
constexpr uint32_t DimZ = 4;
}

namespace heatpoints {
constexpr uint32_t InitialTemperature = 1;
}
}

class NodeGraphRegistry {
public:
    NodeGraphRegistry() = default;

    uint8_t registerPayloadType(const std::string& name, NodeGraphValueType displayType);
    NodeGraphValueType getPayloadDisplayType(uint8_t typeId) const;
    const std::string* getPayloadTypeName(uint8_t typeId) const;

    void registerNodeType(NodeTypeDefinition definition);
    const NodeTypeDefinition* findNodeType(const NodeTypeId& typeId) const;
    const std::vector<NodeTypeDefinition>& allNodeTypes() const;

    const NodeGraphTypeRegistry& typeRegistry() const { return payloadTypes; }

private:
    NodeGraphTypeRegistry payloadTypes;
    std::unordered_map<std::string, NodeTypeDefinition> nodeTypes;
    std::vector<NodeTypeDefinition> nodeList;
};
