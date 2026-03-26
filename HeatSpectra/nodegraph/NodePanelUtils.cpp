#include "NodePanelUtils.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"

#include "NodeGraphBridge.hpp"
#include "NodeGraphEditor.hpp"

#include <cctype>
#include <filesystem>
#include <limits>
#include <sstream>

namespace NodePanelUtils {

namespace {

bool writeParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, const NodeGraphParamValue& parameter) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphEditor editor(*nodeGraphBridge);
    return editor.setNodeParameter(nodeId, parameter);
}

}

bool readBoolParam(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue) {
    bool value = defaultValue;
    if (tryGetNodeParamBool(node, parameterId, value)) {
        return value;
    }
    return defaultValue;
}

double readFloatParam(const NodeGraphNode& node, uint32_t parameterId, double defaultValue) {
    double value = defaultValue;
    if (tryGetNodeParamFloat(node, parameterId, value)) {
        return value;
    }
    return defaultValue;
}

int readIntParam(const NodeGraphNode& node, uint32_t parameterId, int defaultValue) {
    int64_t value = defaultValue;
    if (tryGetNodeParamInt(node, parameterId, value)) {
        return static_cast<int>(value);
    }
    return defaultValue;
}

std::string readStringParam(const NodeGraphNode& node, uint32_t parameterId) {
    std::string value;
    if (tryGetNodeParamString(node, parameterId, value)) {
        return value;
    }
    return {};
}

bool writeBoolParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Bool;
    parameter.boolValue = value;
    return writeParam(nodeGraphBridge, nodeId, parameter);
}

bool writeFloatParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, double value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Float;
    parameter.floatValue = value;
    return writeParam(nodeGraphBridge, nodeId, parameter);
}

bool writeIntParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, int64_t value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::Int;
    parameter.intValue = value;
    return writeParam(nodeGraphBridge, nodeId, parameter);
}

bool writeStringParam(NodeGraphBridge* nodeGraphBridge, NodeGraphNodeId nodeId, uint32_t parameterId, const std::string& value) {
    if (!nodeGraphBridge) {
        return false;
    }

    NodeGraphParamValue parameter{};
    parameter.id = parameterId;
    parameter.type = NodeGraphParamType::String;
    parameter.stringValue = value;
    return writeParam(nodeGraphBridge, nodeId, parameter);
}

std::string trimCopy(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string toLowerCopy(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string stripLineComment(const std::string& value) {
    const std::size_t commentIndex = value.find('#');
    if (commentIndex == std::string::npos) {
        return value;
    }
    return value.substr(0, commentIndex);
}

std::string normalizePresetName(const std::string& value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (char character : value) {
        const unsigned char u = static_cast<unsigned char>(character);
        if (std::isalnum(u) == 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(u)));
    }
    return normalized;
}

bool tryParseUint32Id(const std::string& value, uint32_t& outValue) {
    const std::string trimmed = trimCopy(value);
    if (trimmed.empty()) {
        return false;
    }

    for (char character : trimmed) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return false;
        }
    }

    try {
        const unsigned long parsed = std::stoul(trimmed);
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        outValue = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

void appendDelimitedNames(
    const std::string& list,
    std::unordered_set<std::string>& seenNames,
    std::vector<std::string>& outNames) {
    std::stringstream listStream(list);
    std::string token;
    while (std::getline(listStream, token, ';')) {
        token = trimCopy(token);
        if (token.empty()) {
            continue;
        }

        if (seenNames.insert(token).second) {
            outNames.push_back(token);
        }
    }
}

void collectUpstreamModelPaths(
    const NodeGraphState& state,
    NodeGraphNodeId nodeId,
    std::unordered_set<uint32_t>& visitedNodeIds,
    std::unordered_set<std::string>& seenPaths,
    std::vector<std::string>& outModelPaths) {
    if (!nodeId.isValid() || !visitedNodeIds.insert(nodeId.value).second) {
        return;
    }

    const NodeGraphNode* node = ::findNodeInState(state, nodeId);
    if (!node) {
        return;
    }

    if (getNodeTypeId(node->typeId) == nodegraphtypes::Model) {
        const std::string modelPath = readStringParam(*node, nodegraphparams::model::Path);
        if (!modelPath.empty() && seenPaths.insert(modelPath).second) {
            outModelPaths.push_back(modelPath);
        }
        return;
    }

    for (const NodeGraphSocket& inputSocket : node->inputs) {
        const NodeGraphEdge* inputEdge = ::findIncomingEdgeInState(state, node->id, inputSocket.id);
        if (!inputEdge) {
            continue;
        }
        collectUpstreamModelPaths(state, inputEdge->fromNode, visitedNodeIds, seenPaths, outModelPaths);
    }
}

void collectUpstreamModelNodeIds(
    const NodeGraphState& state,
    NodeGraphNodeId nodeId,
    std::unordered_set<uint32_t>& visitedNodeIds,
    std::unordered_set<uint32_t>& seenModelNodeIds,
    std::vector<uint32_t>& outModelNodeIds) {
    if (!nodeId.isValid() || !visitedNodeIds.insert(nodeId.value).second) {
        return;
    }

    const NodeGraphNode* node = ::findNodeInState(state, nodeId);
    if (!node) {
        return;
    }

    if (getNodeTypeId(node->typeId) == nodegraphtypes::Model) {
        if (node->id.isValid() && seenModelNodeIds.insert(node->id.value).second) {
            outModelNodeIds.push_back(node->id.value);
        }
        return;
    }

    for (const NodeGraphSocket& inputSocket : node->inputs) {
        const NodeGraphEdge* inputEdge = ::findIncomingEdgeInState(state, node->id, inputSocket.id);
        if (!inputEdge) {
            continue;
        }
        collectUpstreamModelNodeIds(state, inputEdge->fromNode, visitedNodeIds, seenModelNodeIds, outModelNodeIds);
    }
}

std::vector<std::string> resolveCandidateModelPaths(const std::string& modelPath) {
    std::vector<std::string> candidates;
    if (modelPath.empty()) {
        return candidates;
    }

    std::unordered_set<std::string> seenPaths;
    auto addCandidate = [&candidates, &seenPaths](const std::filesystem::path& path) {
        const std::string candidate = path.lexically_normal().string();
        if (!candidate.empty() && seenPaths.insert(candidate).second) {
            candidates.push_back(candidate);
        }
    };

    const std::filesystem::path rawPath(modelPath);
    addCandidate(rawPath);

    if (!rawPath.is_absolute()) {
        const std::filesystem::path currentPath = std::filesystem::current_path();
        addCandidate(currentPath / rawPath);
        addCandidate(currentPath / "HeatSpectra" / rawPath);
    }

    return candidates;
}

} 
