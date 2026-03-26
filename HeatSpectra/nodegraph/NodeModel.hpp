#pragma once

#include "NodeGraphKernels.hpp"

#include <cstdint>
#include <utility>

class NodeModel final : public NodeKernel {
public:
    const char* typeId() const override;
    bool execute(NodeGraphKernelContext& context) const override;
    bool computeInputHash(const NodeGraphKernelHashContext& context, uint64_t& outHash) const override;

private:
    static bool parseObjGeometry(const std::string& modelPath, GeometryData& geometry);
    static bool populateGeometryFromModelPath(const std::string& modelPath, GeometryData& geometry);
    static bool loadGeometryFromModelPath(const std::string& modelPath, GeometryData& geometry);
    static std::vector<std::string> resolveCandidateModelPaths(const std::string& modelPath);
};
