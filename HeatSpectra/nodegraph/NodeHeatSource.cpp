#include "NodeHeatSource.hpp"

const char* NodeHeatSource::typeId() const {
    return nodegraphtypes::HeatSource;
}

bool NodeHeatSource::execute(NodeGraphKernelContext& context) const {
    const NodeDataBlock* inputGeometryValue = nullptr;
    for (const NodeDataBlock* inputValue : context.inputs) {
        if (inputValue && inputValue->dataType == NodeDataType::Geometry) {
            inputGeometryValue = inputValue;
            break;
        }
    }

    for (NodeDataBlock& outputValue : context.outputs) {
        outputValue.dataType = NodeDataType::None;
        outputValue.geometry = {};
        if (!inputGeometryValue) {
            refreshNodeDataBlockMetadata(outputValue);
            continue;
        }

        outputValue.dataType = NodeDataType::HeatSource;
        outputValue.geometry = inputGeometryValue->geometry;
        setDetailBoolAttribute(outputValue.geometry, "source.active", true);
        setPointFloatAttributeConstant(outputValue.geometry, "temperature", 0.0f);
        refreshNodeDataBlockMetadata(outputValue);
    }

    return false;
}
