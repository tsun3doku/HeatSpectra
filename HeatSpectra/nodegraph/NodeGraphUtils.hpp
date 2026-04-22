#pragma once

#include "NodeGraphDataTypes.hpp"
#include <string>

struct NodeGraphKernelExecutionState;

uint64_t makeSocketKey(NodeGraphNodeId nodeId, NodeGraphSocketId socketId);
bool tryDecodeSocketKey(uint64_t socketKey, NodeGraphNodeId& outNodeId, NodeGraphSocketId& outSocketId);
const NodeGraphNode* findNodeInState(const NodeGraphState& state, NodeGraphNodeId nodeId);
const NodeGraphEdge* findIncomingEdgeInState(const NodeGraphState& state, NodeGraphNodeId toNodeId, NodeGraphSocketId toSocketId);
const NodeGraphSocket* findInputSocket(const NodeGraphNode& node, const char* socketName);
const NodeGraphSocket* findInputSocket(const NodeGraphNode& node, NodeGraphValueType valueType);
const NodeGraphSocket* findOutputSocketProducingPayload(const NodeGraphNode& node, NodePayloadType payloadType);
const EvaluatedSocketValue* readEvaluatedInput(const NodeGraphNode& node, NodeGraphSocketId inputSocketId, const NodeGraphKernelExecutionState& executionState);
const NodeDataBlock* readInputValue(const EvaluatedSocketValue* input);

NodeGraphParamValue makeNodeGraphParamValue(const NodeGraphParamDefinition& definition);
const NodeGraphParamValue* findNodeParamValue(const NodeGraphNode& node, uint32_t paramId);
NodeGraphParamValue* findNodeParamValue(NodeGraphNode& node, uint32_t paramId);
bool validateNodeGraphParamValue(const NodeGraphParamDefinition& definition, const NodeGraphParamValue& value);

std::string displayNodeLabel(const NodeGraphNode& node);

NodeTypeId getNodeTypeId(const NodeTypeId& requestedTypeId);
const NodeGraphParamDefinition* findNodeParamDefinition(const NodeTypeDefinition& definition, uint32_t paramId);

bool tryGetNodeParamFloat(const NodeGraphNode& node, uint32_t paramId, double& outValue);
bool tryGetNodeParamInt(const NodeGraphNode& node, uint32_t paramId, int64_t& outValue);
bool tryGetNodeParamBool(const NodeGraphNode& node, uint32_t paramId, bool& outValue);
bool tryGetNodeParamString(const NodeGraphNode& node, uint32_t paramId, std::string& outValue);
bool tryGetNodeParamEnum(const NodeGraphNode& node, uint32_t paramId, std::string& outValue);
