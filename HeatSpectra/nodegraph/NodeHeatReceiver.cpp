#include "NodeHeatReceiver.hpp"

const char* NodeHeatReceiver::typeId() const {
    return nodegraphtypes::HeatReceiver;
}

bool NodeHeatReceiver::execute(NodeGraphKernelContext& context) const {
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

        outputValue.dataType = NodeDataType::HeatReceiver;
        outputValue.geometry = inputGeometryValue->geometry;
        setDetailBoolAttribute(outputValue.geometry, "receiver.active", true);
        refreshNodeDataBlockMetadata(outputValue);
    }

    return false;
}
