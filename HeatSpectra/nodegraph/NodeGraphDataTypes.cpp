#include "NodeGraphDataTypes.hpp"

#include <cstddef>
#include <unordered_set>

const char* nodeDataTypeToString(NodeDataType dataType) {
    switch (dataType) {
    case NodeDataType::Geometry:
        return "geometry";
    case NodeDataType::HeatReceiver:
        return "heat_receiver";
    case NodeDataType::HeatSource:
        return "heat_source";
    case NodeDataType::ScalarFloat:
        return "scalar_float";
    case NodeDataType::ScalarInt:
        return "scalar_int";
    case NodeDataType::ScalarBool:
        return "scalar_bool";
    case NodeDataType::None:
    default:
        return "none";
    }
}

namespace {

GeometryAttribute* findAttribute(
    GeometryData& geometry,
    const std::string& name,
    GeometryAttributeDomain domain) {
    for (GeometryAttribute& attribute : geometry.attributes) {
        if (attribute.name == name && attribute.domain == domain) {
            return &attribute;
        }
    }
    return nullptr;
}

std::string geometryAttributeSummary(const GeometryData& geometry) {
    if (geometry.attributes.empty()) {
        return "none";
    }

    std::string summary;
    for (std::size_t index = 0; index < geometry.attributes.size(); ++index) {
        if (index > 0) {
            summary += ", ";
        }
        summary += geometry.attributes[index].name;
        if (index >= 7 && geometry.attributes.size() > 8) {
            summary += ", ...";
            break;
        }
    }
    return summary;
}

} // namespace

void setDetailBoolAttribute(GeometryData& geometry, const std::string& name, bool value) {
    GeometryAttribute* attribute = findAttribute(geometry, name, GeometryAttributeDomain::Detail);
    if (!attribute) {
        geometry.attributes.push_back({});
        attribute = &geometry.attributes.back();
    }

    attribute->name = name;
    attribute->domain = GeometryAttributeDomain::Detail;
    attribute->dataType = GeometryAttributeDataType::Bool;
    attribute->tupleSize = 1;
    attribute->floatValues.clear();
    attribute->intValues.clear();
    attribute->boolValues.assign(1, value ? 1 : 0);
}

void setPointFloatAttributeConstant(GeometryData& geometry, const std::string& name, float value) {
    GeometryAttribute* attribute = findAttribute(geometry, name, GeometryAttributeDomain::Point);
    if (!attribute) {
        geometry.attributes.push_back({});
        attribute = &geometry.attributes.back();
    }

    attribute->name = name;
    attribute->domain = GeometryAttributeDomain::Point;
    attribute->dataType = GeometryAttributeDataType::Float;
    attribute->tupleSize = 1;
    attribute->intValues.clear();
    attribute->boolValues.clear();
    const std::size_t pointCount = geometry.pointPositions.size() / 3;
    attribute->floatValues.assign(pointCount, value);
}

void refreshNodeDataBlockMetadata(NodeDataBlock& dataBlock) {
    dataBlock.metadata["data.type"] = nodeDataTypeToString(dataBlock.dataType);

    if (dataBlock.dataType == NodeDataType::Geometry ||
        dataBlock.dataType == NodeDataType::HeatReceiver ||
        dataBlock.dataType == NodeDataType::HeatSource) {
        if (dataBlock.geometry.sourceModelPath.empty()) {
            dataBlock.metadata.erase("geometry.model_path");
        } else {
            dataBlock.metadata["geometry.model_path"] = dataBlock.geometry.sourceModelPath;
        }
        dataBlock.metadata["geometry.model_id"] = std::to_string(dataBlock.geometry.modelId);
        dataBlock.metadata["geometry.point_count"] = std::to_string(dataBlock.geometry.pointPositions.size() / 3);
        dataBlock.metadata["geometry.triangle_count"] = std::to_string(dataBlock.geometry.triangleIndices.size() / 3);
        dataBlock.metadata["geometry.attribute_count"] = std::to_string(dataBlock.geometry.attributes.size());
        dataBlock.metadata["geometry.attributes"] = geometryAttributeSummary(dataBlock.geometry);
    } else {
        dataBlock.metadata.erase("geometry.model_path");
        dataBlock.metadata.erase("geometry.model_id");
        dataBlock.metadata.erase("geometry.point_count");
        dataBlock.metadata.erase("geometry.triangle_count");
        dataBlock.metadata.erase("geometry.attribute_count");
        dataBlock.metadata.erase("geometry.attributes");
    }
}

void seedOutputDataBlocksFromInputs(
    const NodeGraphNode& node,
    const std::vector<const NodeDataBlock*>& inputs,
    std::vector<NodeDataBlock>& outputs) {
    std::unordered_map<std::string, std::string> mergedMetadata;
    std::vector<NodeGraphNodeId> mergedLineage;
    std::unordered_set<uint32_t> seenNodeIds;

    for (const NodeDataBlock* input : inputs) {
        if (!input) {
            continue;
        }

        for (const auto& metadataEntry : input->metadata) {
            mergedMetadata[metadataEntry.first] = metadataEntry.second;
        }

        for (NodeGraphNodeId lineageNodeId : input->lineageNodeIds) {
            if (!lineageNodeId.isValid()) {
                continue;
            }

            if (seenNodeIds.insert(lineageNodeId.value).second) {
                mergedLineage.push_back(lineageNodeId);
            }
        }
    }

    if (node.id.isValid() && seenNodeIds.insert(node.id.value).second) {
        mergedLineage.push_back(node.id);
    }

    mergedMetadata["graph.producer_node_id"] = std::to_string(node.id.value);
    mergedMetadata["graph.producer_type_id"] = canonicalNodeTypeId(node.typeId);
    mergedMetadata["graph.lineage_depth"] = std::to_string(mergedLineage.size());

    for (NodeDataBlock& output : outputs) {
        output = {};
        output.metadata = mergedMetadata;
        output.lineageNodeIds = mergedLineage;
        refreshNodeDataBlockMetadata(output);
    }
}
