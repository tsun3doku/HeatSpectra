#pragma once

#include "heat/HeatContactSolver.hpp"
#include "contact/ContactTypes.hpp"
#include "framegraph/ComputePass.hpp"
#include "util/Units.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

class VulkanDevice;
class HeatModelRuntime;
class VoronoiNodeIndex;

class HeatContactRuntime {
public:
    static constexpr float FixedTimeStep = 1.0f / 60.0f;

    HeatContactRuntime() = default;
    ~HeatContactRuntime();

    HeatContactRuntime(const HeatContactRuntime&) = delete;
    HeatContactRuntime& operator=(const HeatContactRuntime&) = delete;

    bool hasGraph() const { return solver && solver->isInitialized(); }
    bool build(
        VulkanDevice& vulkanDevice,
        const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
        const std::vector<ContactCoupling>& couplings,
        float heatTransferCoefficient);

    bool solve(bool temperatureBufferAIsCurrent);
    const std::vector<float>* findCoveredAreas(uint32_t runtimeModelId) const;
    ComputePass::Synchronization getSynchronization() const;
    void clearSynchronization();
    void cleanup();

private:
    struct SolverModelNodes {
        uint32_t runtimeModelId = 0;
        uint32_t solverNodeOffset = 0;
        std::vector<uint32_t> localNodeIds;
    };

    struct SampleNode {
        uint32_t fullNodeId = 0;
        double weight = 0.0;
    };

    struct ContactSampleData {
        double conductance = 0.0;
        std::vector<SampleNode> nodes;
    };

    struct FixedBoundaryRegion {
        HeatModelRuntime* model = nullptr;
        uint32_t regionId = 0;
    };

    static void buildWendlandWeights(
        const glm::vec3& point,
        const VoronoiNodeIndex& nodeIndex,
        std::vector<uint32_t>& neighborIds,
        std::vector<float>& weights);

    std::unique_ptr<HeatContactSolver> solver;
    std::vector<SolverModelNodes> modelNodes;
    std::vector<FixedBoundaryRegion> fixedBoundaryRegions;
    std::unordered_map<uint32_t, std::vector<float>> coveredAreasByModelId;
    ComputePass::Synchronization synchronization{};
    uint64_t timelineValue = 0;
    uint64_t previousVulkanValue = 0;
};
