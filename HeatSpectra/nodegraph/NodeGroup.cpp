#include "NodeGroup.hpp"
#include "NodeGraphRegistry.hpp"
#include "NodeGraphUtils.hpp"
#include "NodePanelUtils.hpp"
#include "NodePayloadRegistry.hpp"

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

    const bool enabled = NodePanelUtils::readBoolParam(context.node, nodegraphparams::group::Enabled, true);
    const std::string sourceGroupName = NodePanelUtils::readStringParam(context.node, nodegraphparams::group::SourceName);
    const std::string targetGroupName = NodePanelUtils::readStringParam(context.node, nodegraphparams::group::TargetName);
    NodePayloadRegistry* const payloadRegistry = context.executionState.services.payloadRegistry;

    for (std::size_t outputIndex = 0; outputIndex < context.outputs.size(); ++outputIndex) {
        NodeDataBlock& outputValue = context.outputs[outputIndex];
        outputValue.dataType = NodeDataType::None;
        outputValue.payloadHandle = {};
        if (!inputGeometryValue || !payloadRegistry) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        const GeometryData* inputGeometry = payloadRegistry->get<GeometryData>(inputGeometryValue->payloadHandle);
        if (!inputGeometry) {
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        outputValue.dataType = NodeDataType::Geometry;
        if (!enabled || sourceGroupName.empty() || targetGroupName.empty()) {
            outputValue.payloadHandle = inputGeometryValue->payloadHandle;
            updateDataBlockMetadata(outputValue, payloadRegistry);
            continue;
        }

        GeometryData updatedGeometry = *inputGeometry;
        normalizeGeometryGroups(updatedGeometry);
        const bool changed = applyAssignment(updatedGeometry, sourceGroupName, targetGroupName);
        normalizeGeometryGroups(updatedGeometry);
        if (changed) {
            bumpGeometryRevision(updatedGeometry);
            const uint64_t payloadKey = makeSocketKey(
                context.node.id,
                context.node.outputs[outputIndex].id);
            outputValue.payloadHandle = payloadRegistry->upsert(payloadKey, std::move(updatedGeometry));
        } else {
            outputValue.payloadHandle = inputGeometryValue->payloadHandle;
        }
        updateDataBlockMetadata(outputValue, payloadRegistry);
    }

    return false;
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

bool NodeGroup::applyAssignment(
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
        return false;
    }

    const uint32_t targetGroupId = resolveTargetGroupId(geometry, targetGroupName);
    bool changed = false;

    for (uint32_t& triangleGroupId : geometry.triangleGroupIds) {
        if (matchingGroupIds.find(triangleGroupId) != matchingGroupIds.end()) {
            if (triangleGroupId != targetGroupId) {
                triangleGroupId = targetGroupId;
                changed = true;
            }
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
        changed = true;
    } else {
        if (existingIt->name != targetGroupName) {
            existingIt->name = targetGroupName;
            changed = true;
        }
        if (existingIt->source.empty() || existingIt->source == "generated") {
            existingIt->source = "node.group";
            changed = true;
        }
    }

    return changed;
}
