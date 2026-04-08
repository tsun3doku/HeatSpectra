#pragma once

#include "NodeGraphTypes.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

class NodeGraphBridge;

namespace NodePanelUtils {

bool canEditNode(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId);
bool loadNode(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, NodeGraphNode& outNode);

bool readBoolParam(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue = false);
double readFloatParam(const NodeGraphNode& node, uint32_t parameterId, double defaultValue);
int readIntParam(const NodeGraphNode& node, uint32_t parameterId, int defaultValue);
std::string readStringParam(const NodeGraphNode& node, uint32_t parameterId);

bool writeBoolParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value);
bool writeFloatParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, double value);
bool writeIntParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, int64_t value);
bool writeStringParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, const std::string& value);

std::string trimCopy(const std::string& value);
std::string toLowerCopy(std::string value);
std::string stripLineComment(const std::string& value);
std::string normalizePresetName(const std::string& value);
bool tryParseUint32Id(const std::string& value, uint32_t& outValue);

void appendDelimitedNames(const std::string& list, std::unordered_set<std::string>& seenNames, std::vector<std::string>& outNames);

void collectUpstreamModelPaths(
    const NodeGraphState& state,
    NodeGraphNodeId nodeId,
    std::unordered_set<uint32_t>& visitedNodeIds,
    std::unordered_set<std::string>& seenPaths,
    std::vector<std::string>& outModelPaths);

void findUpstreamModelNodeIds(
    const NodeGraphState& state,
    NodeGraphNodeId nodeId,
    std::unordered_set<uint32_t>& visitedNodeIds,
    std::unordered_set<uint32_t>& seenModelNodeIds,
    std::vector<uint32_t>& outModelNodeIds);

std::vector<std::string> resolveCandidateModelPaths(const std::string& modelPath);

} 
