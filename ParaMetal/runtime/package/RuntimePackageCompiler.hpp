#pragma once

#include "hash/HashValues.hpp"
#include "nodegraph/NodeGraphCoreTypes.hpp"
#include "util/Units.hpp"

#include <string>
#include <vector>

class NodePayloadRegistry;
class RuntimePackageManager;
class RuntimeProductManager;
struct NodeGraphCompiled;
struct NodeDataBlock;
struct NodeGraphEvaluation;
struct NodeGraphNode;
struct NodeGraphState;

class RuntimePackageCompiler {
public:
    enum class FrozenPackages {
        Preserve,
        Rebuild,
    };

    bool validate(
        const NodeGraphState& graph,
        const NodeGraphCompiled& graphPlan,
        const NodeGraphEvaluation& evaluation,
        const NodePayloadRegistry& payloads,
        std::vector<std::string>& errors) const;

    bool compileNode(
        const NodeGraphState& graph,
        const NodeGraphNode& node,
        const NodeGraphEvaluation& evaluation,
        const NodePayloadRegistry& payloads,
        const RuntimeProductManager& products,
        units::LengthUnit worldUnit,
        FrozenPackages frozenPackages,
        RuntimePackageManager& packages,
        std::vector<std::string>& errors) const;

private:
    bool compileModelPackage(
        uint64_t outputSocketKey,
        const NodeDataBlock& output,
        const NodePayloadRegistry& payloads,
        units::LengthUnit worldUnit,
        RuntimePackageManager& packages) const;
    bool compilePointPackage(
        const NodeGraphState& graph,
        const NodeGraphNode& node,
        uint64_t outputSocketKey,
        const NodeDataBlock& output,
        const NodePayloadRegistry& payloads,
        const RuntimeProductManager& products,
        units::LengthUnit worldUnit,
        RuntimePackageManager& packages,
        std::vector<std::string>& errors) const;
    bool compileRemeshPackage(
        const NodeGraphNode& node,
        uint64_t outputSocketKey,
        const NodeDataBlock& output,
        const NodePayloadRegistry& payloads,
        units::LengthUnit worldUnit,
        RuntimePackageManager& packages,
        std::vector<std::string>& errors) const;
    bool compileVoronoiPackage(
        const NodeGraphNode& node,
        uint64_t outputSocketKey,
        const NodeDataBlock& output,
        const NodePayloadRegistry& payloads,
        units::LengthUnit worldUnit,
        RuntimePackageManager& packages,
        std::vector<std::string>& errors) const;
    bool compileContactPackage(
        const NodeGraphNode& node,
        uint64_t outputSocketKey,
        const NodeDataBlock& output,
        const NodePayloadRegistry& payloads,
        units::LengthUnit worldUnit,
        RuntimePackageManager& packages,
        std::vector<std::string>& errors) const;
    bool compileHeatPackage(
        const NodeGraphNode& node,
        uint64_t outputSocketKey,
        const NodeDataBlock& output,
        const NodePayloadRegistry& payloads,
        units::LengthUnit worldUnit,
        RuntimePackageManager& packages,
        std::vector<std::string>& errors) const;
    static HashValues resolveHandleHashes(
        const NodePayloadRegistry& payloads,
        const NodeDataHandle& handle);
};
