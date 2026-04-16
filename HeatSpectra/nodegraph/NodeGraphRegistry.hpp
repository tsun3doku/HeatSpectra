#pragma once

#include "NodeGraphTypes.hpp"
#include <vector>

namespace nodegraphtypes {
inline constexpr const char* Model = "model";
inline constexpr const char* Transform = "transform";
inline constexpr const char* Group = "group";
inline constexpr const char* Remesh = "remesh";
inline constexpr const char* HeatReceiver = "heat_receiver";
inline constexpr const char* HeatSource = "heat_source";
inline constexpr const char* Contact = "contact";
inline constexpr const char* Voronoi = "voronoi";
inline constexpr const char* HeatSolve = "heat_solve";
inline constexpr const char* Custom = "custom";
}

namespace nodegraphparams {
namespace model {
constexpr uint32_t Path = 1;
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
constexpr uint32_t MaterialBindings = 4;
constexpr uint32_t CellSize = 6;
constexpr uint32_t VoxelResolution = 7;
constexpr uint32_t ShowHeatOverlay = 8;
}

namespace voronoi {
constexpr uint32_t CellSize = 1;
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
}

class NodeGraphRegistry {
public:
    static const std::vector<NodeTypeDefinition>& getBuiltInNodes();
    static const NodeTypeDefinition* findNodeById(const NodeTypeId& typeId);

private:
    static NodeGraphAttributeContract makeAttributeContract(
        const char* name,
        GeometryAttributeDomain domain,
        GeometryAttributeDataType dataType = GeometryAttributeDataType::Float,
        uint32_t tupleSize = 1);

    static NodeSocketSignature makeInputSocket(
        const char* name,
        NodeGraphValueType valueType,
        std::vector<NodeGraphAttributeContract> requiredAttributes = {});

    static NodeSocketSignature makeOutputSocket(
        const char* name,
        NodeGraphValueType valueType,
        NodePayloadType producedPayloadType,
        std::vector<NodeGraphAttributeContract> guaranteedAttributes = {});

    static NodeTypeDefinition buildModelNode();
    static NodeTypeDefinition buildTransformNode();
    static NodeTypeDefinition buildGroupNode();
    static NodeTypeDefinition buildRemeshNode();
    static NodeTypeDefinition buildHeatReceiverNode();
    static NodeTypeDefinition buildHeatSourceNode();
    static NodeTypeDefinition buildContactNode();
    static NodeTypeDefinition buildVoronoiNode();
    static NodeTypeDefinition buildHeatSolveNode();
    static NodeTypeDefinition buildCustomNode();
};
