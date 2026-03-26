#pragma once

#include "NodeGraphTypes.hpp"

#include <vector>

namespace nodegraphsceneutils {

NodeGraphEdgeId findIncomingEdgeForInput(
    const NodeGraphState& state,
    NodeGraphNodeId nodeId,
    NodeGraphSocketId socketId);
const NodeGraphNode* findStateNodeById(const NodeGraphState& state, NodeGraphNodeId nodeId);
int findSocketIndexById(const std::vector<NodeGraphSocket>& sockets, NodeGraphSocketId socketId);
NodeGraphSocketId socketByIndex(const std::vector<NodeGraphSocket>& sockets, int index);
std::vector<NodeGraphSocketId> matchingInputSocketsByType(
    const NodeGraphNode& node,
    NodeGraphValueType valueType);
int valueTypeOrdinalAtInputIndex(const NodeGraphNode& node, int inputIndex);

} // namespace nodegraphsceneutils
