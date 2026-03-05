#pragma once

#include "NodeGraphKernels.hpp"

#include <cstdint>
#include <utility>

class NodeModel final : public NodeKernel {
public:
    const char* typeId() const override;
    bool execute(NodeGraphKernelContext& context) const override;

private:
    static bool getBoolParamValue(const NodeGraphNode& node, uint32_t parameterId, bool defaultValue = false);
    static std::string getStringParamValue(const NodeGraphNode& node, uint32_t parameterId);
    static bool setBoolParameter(NodeGraphBridge& bridge, NodeGraphNodeId nodeId, uint32_t parameterId, bool value);
    static bool parseObjGeometry(const std::string& modelPath, GeometryData& geometry);
    static bool tryResolveLoadableModelPath(const std::string& modelPath, std::string& outResolvedPath);
    static bool populateGeometryFromModelPath(const std::string& modelPath, GeometryData& geometry);
    static bool loadGeometryFromModelPath(const std::string& modelPath, GeometryData& geometry);
    static std::vector<std::string> resolveCandidateModelPaths(const std::string& modelPath);
};
