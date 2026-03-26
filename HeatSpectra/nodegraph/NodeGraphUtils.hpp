#pragma once

#include "NodeGraphDataTypes.hpp"
#include <string>

struct NodeGraphKernelExecutionState;

uint64_t makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);
const NodeGraphNode* findNodeInState(const NodeGraphState& state, NodeGraphNodeId nodeId);
const NodeGraphEdge* findIncomingEdgeInState(
    const NodeGraphState& state,
    NodeGraphNodeId toNodeId,
    NodeGraphSocketId toSocketId);
const NodeGraphSocket* findInputSocket(const NodeGraphNode& node, const char* socketName);
const NodeGraphSocket* findInputSocket(const NodeGraphNode& node, NodeGraphValueType valueType);
const NodeDataBlock* readInput(
    const NodeGraphNode& node,
    NodeGraphSocketId inputSocketId,
    const NodeGraphKernelExecutionState& executionState);

NodeGraphParamValue makeNodeGraphParamValue(const NodeGraphParamDefinition& definition);
const NodeGraphParamValue* findNodeParamValue(const NodeGraphNode& node, uint32_t paramId);
NodeGraphParamValue* findNodeParamValue(NodeGraphNode& node, uint32_t paramId);

std::string displayNodeLabel(const NodeGraphNode& node);

NodeTypeId getNodeTypeId(const NodeTypeId& requestedTypeId);
const NodeGraphParamDefinition* findNodeParamDefinition(const NodeTypeDefinition& definition, uint32_t paramId);

bool tryGetNodeParamFloat(const NodeGraphNode& node, uint32_t paramId, double& outValue);
bool tryGetNodeParamInt(const NodeGraphNode& node, uint32_t paramId, int64_t& outValue);
bool tryGetNodeParamBool(const NodeGraphNode& node, uint32_t paramId, bool& outValue);
bool tryGetNodeParamString(const NodeGraphNode& node, uint32_t paramId, std::string& outValue);
