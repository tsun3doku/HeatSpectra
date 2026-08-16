#pragma once

#include "render/RenderSettings.hpp"
#include "scene/Camera.hpp"
#include "nodegraph/NodeGraphEditor.hpp"
#include "nodegraph/NodeGraphState.hpp"
#include "nodegraph/NodeGraphTypes.hpp"

#include <QMetaType>
#include <QString>

#include <cstdint>
#include <vector>

struct ViewportUiState {
    app::WireframeMode wireframeMode = app::WireframeMode::Off;
    bool gridEnabled = app::RenderSettings{}.gridEnabled;
    app::BackgroundMode backgroundMode = app::RenderSettings{}.backgroundMode;
    bool navigationCubeVisible = app::RenderSettings{}.navigationCubeVisible;
    bool axisLabelsVisible = app::RenderSettings{}.axisLabelsVisible;
    CameraProjectionMode projectionMode = CameraProjectionMode::Perspective;
    float baseFov = Camera::DefaultFov;
    float zoomSpeed = Camera::DefaultZoomSpeed;
    float panSpeed = Camera::DefaultPanSpeed;

    bool operator==(const ViewportUiState& other) const {
        return wireframeMode == other.wireframeMode &&
               gridEnabled == other.gridEnabled &&
               backgroundMode == other.backgroundMode &&
               navigationCubeVisible == other.navigationCubeVisible &&
               axisLabelsVisible == other.axisLabelsVisible &&
               projectionMode == other.projectionMode &&
               baseFov == other.baseFov &&
               zoomSpeed == other.zoomSpeed &&
               panSpeed == other.panSpeed;
    }
    bool operator!=(const ViewportUiState& other) const { return !(*this == other); }
};

struct HeatPaletteUiState {
    bool visible = false;
    int paletteId = 0;
    int units = 0;
    double minimumC = 0.0;
    double maximumC = 100.0;
    double x = -1.0;
    double y = -1.0;
};

struct TimelineUiState {
    bool playing = false;
    uint32_t currentFrame = 0;
    uint32_t frameCount = 251;
    uint32_t recordedFrames = 0;
    uint32_t startFrame = 0;
    uint32_t endFrame = 250;
    double currentSeconds = 0.0;
    double durationSeconds = 250.0 / 60.0;

    bool operator==(const TimelineUiState& other) const {
        return playing == other.playing && currentFrame == other.currentFrame &&
               frameCount == other.frameCount && recordedFrames == other.recordedFrames &&
               startFrame == other.startFrame &&
               endFrame == other.endFrame && currentSeconds == other.currentSeconds &&
               durationSeconds == other.durationSeconds;
    }
    bool operator!=(const TimelineUiState& other) const { return !(*this == other); }
};

struct GraphPastePayload {
    std::vector<NodeGraphEditor::CopiedNode> nodes;
    std::vector<NodeGraphEditor::CopiedEdge> edges;
    float offset = 40.0f;
};

struct PythonResult {
    QString output;
    QString error;
    bool incomplete = false;
};

Q_DECLARE_METATYPE(ViewportUiState)
Q_DECLARE_METATYPE(HeatPaletteUiState)
Q_DECLARE_METATYPE(TimelineUiState)
Q_DECLARE_METATYPE(GraphPastePayload)
Q_DECLARE_METATYPE(PythonResult)
Q_DECLARE_METATYPE(NodeGraphState)
Q_DECLARE_METATYPE(NodeGraphDelta)
Q_DECLARE_METATYPE(NodeGraphNodeId)
Q_DECLARE_METATYPE(NodeGraphSocketId)
Q_DECLARE_METATYPE(NodeGraphEdgeId)
Q_DECLARE_METATYPE(NodeGraphParamValue)
Q_DECLARE_METATYPE(std::vector<NodeGraphParamValue>)
Q_DECLARE_METATYPE(std::vector<NodeTypeDefinition>)
Q_DECLARE_METATYPE(std::vector<NodeGraphNodeId>)
