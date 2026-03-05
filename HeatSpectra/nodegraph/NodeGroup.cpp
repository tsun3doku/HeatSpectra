#include "NodeGroup.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>

const char* NodeGroup::typeId() const {
    return nodegraphtypes::Group;
}

bool NodeGroup::execute(NodeGraphKernelContext& context) const {
    const NodeDataBlock* inputGeometryValue = nullptr;
    for (const NodeDataBlock* inputValue : context.inputs) {
        if (inputValue && inputValue->dataType == NodeDataType::Geometry) {
            inputGeometryValue = inputValue;
            break;
        }
    }

    const bool enabled = getBoolParamValue(context.node, nodegraphparams::group::Enabled, true);
    const std::string sourceGroupName = getStringParamValue(context.node, nodegraphparams::group::SourceName);
    const std::string targetGroupName = getStringParamValue(context.node, nodegraphparams::group::TargetName);

    for (NodeDataBlock& outputValue : context.outputs) {
        outputValue.dataType = NodeDataType::None;
        outputValue.geometry = {};
        if (!inputGeometryValue) {
            refreshNodeDataBlockMetadata(outputValue);
            continue;
        }

        outputValue.dataType = NodeDataType::Geometry;
        outputValue.geometry = inputGeometryValue->geometry;
        ensureGeometryGroups(outputValue.geometry);
        if (enabled && !sourceGroupName.empty() && !targetGroupName.empty()) {
            applyAssignment(outputValue.geometry, sourceGroupName, targetGroupName);
            ensureGeometryGroups(outputValue.geometry);
        }

        refreshNodeDataBlockMetadata(outputValue);
    }

    return false;
}

bool NodeGroup::getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue) {
    bool value = defaultValue;
    if (tryGetNodeParamBool(node, parameterId, value)) {
        return value;
    }

    return defaultValue;
}

std::string NodeGroup::getStringParamValue(const NodeGraphNode& node, uint32_t parameterId) {
    std::string value;
    if (tryGetNodeParamString(node, parameterId, value)) {
        return value;
    }

    return {};
}

bool NodeGroup::equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

uint32_t NodeGroup::resolveTargetGroupId(GeometryData& geometry, const std::string& targetGroupName) {
    for (const GeometryGroup& group : geometry.groups) {
        if (equalsIgnoreCase(group.name, targetGroupName)) {
            return group.id;
        }
    }

    uint32_t maxGroupId = 0;
    bool hasAnyGroup = false;
    for (const GeometryGroup& group : geometry.groups) {
        maxGroupId = std::max(maxGroupId, group.id);
        hasAnyGroup = true;
    }

    if (!hasAnyGroup) {
        return 0;
    }

    if (maxGroupId == std::numeric_limits<uint32_t>::max()) {
        return maxGroupId;
    }
    return maxGroupId + 1u;
}

void NodeGroup::applyAssignment(
    GeometryData& geometry,
    const std::string& sourceGroupName,
    const std::string& targetGroupName) {
    std::unordered_set<uint32_t> matchingGroupIds;
    for (const GeometryGroup& group : geometry.groups) {
        if (equalsIgnoreCase(group.name, sourceGroupName)) {
            matchingGroupIds.insert(group.id);
        }
    }

    if (matchingGroupIds.empty()) {
        return;
    }

    const uint32_t targetGroupId = resolveTargetGroupId(geometry, targetGroupName);

    for (uint32_t& triangleGroupId : geometry.triangleGroupIds) {
        if (matchingGroupIds.find(triangleGroupId) != matchingGroupIds.end()) {
            triangleGroupId = targetGroupId;
        }
    }

    auto existingIt = std::find_if(
        geometry.groups.begin(),
        geometry.groups.end(),
        [targetGroupId](const GeometryGroup& group) {
            return group.id == targetGroupId;
        });
    if (existingIt == geometry.groups.end()) {
        GeometryGroup newGroup{};
        newGroup.id = targetGroupId;
        newGroup.name = targetGroupName;
        newGroup.source = "node.group";
        geometry.groups.push_back(std::move(newGroup));
    } else {
        existingIt->name = targetGroupName;
        if (existingIt->source.empty() || existingIt->source == "generated") {
            existingIt->source = "node.group";
        }
    }
}
