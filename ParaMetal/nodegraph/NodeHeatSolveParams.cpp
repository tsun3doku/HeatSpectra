#include "NodeHeatSolveParams.hpp"

#include "heat/HeatGpuStructs.hpp"
#include "NodeGraphEditor.hpp"
#include "NodeGraphRegistry.hpp"
#include "nodegraph/NodeParamUtils.hpp"

HeatSolveNodeParams readHeatSolveNodeParams(const NodeGraphNode& node) {
    HeatSolveNodeParams params{};
    params.enabled = NodeParamUtils::readBoolParam(node, nodegraphparams::heatsolve::Enabled, false);
    params.paused = NodeParamUtils::readBoolParam(node, nodegraphparams::heatsolve::Paused, false);
    params.resetCounter = static_cast<uint32_t>(NodeParamUtils::readIntParam(node, nodegraphparams::heatsolve::ResetRequested, 0));
    params.rewindFrame = static_cast<uint32_t>(NodeParamUtils::readIntParam(node, nodegraphparams::heatsolve::RewindFrame, heat::NoRewindFrame));
    params.contactThermalConductance = NodeParamUtils::readFloatParam(node, nodegraphparams::heatsolve::ContactThermalConductance, 16000.0);
    params.simulationDuration = NodeParamUtils::readFloatParam(node, nodegraphparams::heatsolve::SimulationDuration, 5.0);
    params.preview.showHeatOverlay = NodeParamUtils::readBoolParam(node, nodegraphparams::heatsolve::ShowHeatOverlay, false);
    params.preview.showFluxVectors = NodeParamUtils::readBoolParam(node, nodegraphparams::heatsolve::ShowFluxVectors, false);
    params.preview.showHeatPalette = NodeParamUtils::readBoolParam(node, nodegraphparams::heatsolve::ShowHeatPalette, false);
    params.preview.showContactLevelSet = NodeParamUtils::readBoolParam(node, nodegraphparams::heatsolve::ShowContactLevelSet, false);
    params.preview.fluxVectorScale = NodeParamUtils::readFloatParam(node, nodegraphparams::heatsolve::FluxVectorScale, 1.0);
    params.preview.contactLevelSetRange = NodeParamUtils::readFloatParam(node, nodegraphparams::heatsolve::ContactLevelSetRange, 1.0);

    return params;
}

bool writeHeatSolveNodeParams(NodeGraphEditor& editor, NodeGraphNodeId nodeId, const HeatSolveNodeParams& params) {
    return editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::Enabled, NodeGraphParamType::Bool, 0.0, 0, params.enabled}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::Paused, NodeGraphParamType::Bool, 0.0, 0, params.paused}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::ResetRequested, NodeGraphParamType::Int, 0.0, static_cast<int64_t>(params.resetCounter), false}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::RewindFrame, NodeGraphParamType::Int, 0.0, static_cast<int64_t>(params.rewindFrame), false}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::ContactThermalConductance, NodeGraphParamType::Float, params.contactThermalConductance}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::SimulationDuration, NodeGraphParamType::Float, params.simulationDuration}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::ShowHeatOverlay, NodeGraphParamType::Bool, 0.0, 0, params.preview.showHeatOverlay}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::ShowFluxVectors, NodeGraphParamType::Bool, 0.0, 0, params.preview.showFluxVectors}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::ShowHeatPalette, NodeGraphParamType::Bool, 0.0, 0, params.preview.showHeatPalette}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::ShowContactLevelSet, NodeGraphParamType::Bool, 0.0, 0, params.preview.showContactLevelSet}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::FluxVectorScale, NodeGraphParamType::Float, params.preview.fluxVectorScale}) &&
        editor.setNodeParameter(nodeId, NodeGraphParamValue{nodegraphparams::heatsolve::ContactLevelSetRange, NodeGraphParamType::Float, params.preview.contactLevelSetRange});
}
