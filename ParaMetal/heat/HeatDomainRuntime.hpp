#pragma once

#include "framegraph/ComputePass.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "heat/HeatGlobalSolver.hpp"

class HeatModelRuntime;
class VulkanDevice;

class HeatDomainRuntime {
public:
    static constexpr uint32_t InvalidSolverNode = 0xFFFFFFFFu;

    struct GlobalFragment {
        uint32_t instanceId = 0;
        uint32_t materialId = 0;
        uint32_t seedId = 0;
        float surfaceBoundaryArea = 0.0f;
        float volume = 0.0f;
    };

    struct GlobalFace {
        uint32_t instanceId = 0;
        uint32_t fragmentA = 0;
        uint32_t fragmentB = 0;
        float area = 0.0f;
    };

    struct GlobalCutFace {
        uint32_t fragmentA = 0;
        uint32_t fragmentB = 0;
        float area = 0.0f;
        float gap = 0.0f;
    };

    struct GlobalThermalDomain {
        std::vector<GlobalFragment> fragments;
        std::vector<GlobalFace> faces;
        std::vector<GlobalCutFace> cutFaces;
        std::vector<uint32_t> instanceFragmentCounts;
        std::vector<uint32_t> sdfRuntimeModelIds;
        std::vector<glm::vec3> seedPositions;
        glm::vec3 domainCorners{0.0f, 0.0f, 0.0f};
        glm::vec3 sdfGridMin{0.0f, 0.0f, 0.0f};
        glm::ivec3 sdfGridDim{0, 0, 0};
        float sdfCellSize = 0.0f;
        std::vector<float> sdfValues;

        bool isValid() const {
            return !fragments.empty() && !faces.empty() &&
                sdfGridDim.x > 0 && sdfGridDim.y > 0 && sdfGridDim.z > 0 && sdfCellSize > 0.0f &&
                sdfValues.size() ==
                    static_cast<size_t>(sdfGridDim.x) * static_cast<size_t>(sdfGridDim.y) *
                        static_cast<size_t>(sdfGridDim.z) * instanceFragmentCounts.size();
        }
    };

    ~HeatDomainRuntime();

    const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& getActiveModels() const { return activeModels; }
    HeatModelRuntime* getModelByRuntimeId(uint32_t runtimeModelId) const;
    void removeModelsNotIn(const std::unordered_set<uint32_t>& runtimeModelIds);
    void setModel(uint32_t runtimeModelId, std::unique_ptr<HeatModelRuntime> model);
    void removeModel(uint32_t runtimeModelId);

    void cleanup();

    const GlobalThermalDomain& getGlobalDomain() const { return globalDomain; }
    bool isGlobalDomain() const { return globalDomain.isValid(); }
    void setGlobalDomain(GlobalThermalDomain domain);
    void clearGlobalDomain();

    bool hasGlobalSolver() const { return globalSolver != nullptr; }
    bool allGlobalFragmentsFixed() const { return allGlobalFragmentsFixedFlag; }
    bool globalTopologyDirty() const { return globalTopologyDirtyFlag; }
    bool globalValuesDirty() const { return globalValuesDirtyFlag; }
    void markGlobalTopologyDirty() { globalTopologyDirtyFlag = true; }
    void markGlobalValuesDirty() { globalValuesDirtyFlag = true; }

    bool rebuildGlobalSolver(
        const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
        VulkanDevice& vulkanDevice,
        float fixedTimeStep,
        float contactThermalConductance);
    bool updateGlobalValues(
        const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
        float fixedTimeStep,
        float contactThermalConductance);
    bool launchGlobalSolve(bool sourceIsA);
    bool pollGlobalSolve(bool& completedDestinationIsA);
    bool isGlobalSolveInFlight() const { return globalSolveInFlight; }
    void prepareGlobalRenderSynchronization(bool activeIsA, bool newSolveCompleted);
    void waitGlobalSolveIdle();
    bool hasSolveFailed() const { return globalSolver ? globalSolver->hasSolveFailed() : false; }

    ComputePass::Synchronization getGlobalSynchronization() const { return globalSynchronization; }
    void clearGlobalSynchronization() { globalSynchronization = ComputePass::Synchronization{}; }

private:
    struct GlobalBoundaryRegion {
        const HeatModelRuntime* model = nullptr;
        uint32_t regionId = 0;
        bool dirichlet = false;
    };

    struct SolverModelNodes {
        uint32_t runtimeModelId = 0;
        uint32_t solverNodeOffset = 0;
        std::vector<uint32_t> localNodeIds;
    };

    bool assembleGlobalMatrix(
        const std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>>& models,
        float fixedTimeStep,
        float contactThermalConductance,
        std::vector<int>& rowOffsets,
        std::vector<int>& columnIndices,
        std::vector<float>& values,
        std::vector<float>& thermalMasses,
        std::vector<SolverModelNodes>& solverModelNodes,
        std::vector<HeatGlobalSolver::FixedRow>& fixedRows,
        std::vector<HeatGlobalSolver::FixedContribution>& fixedContributions,
        uint32_t& boundaryValueCount);

    std::unordered_map<uint32_t, std::unique_ptr<HeatModelRuntime>> activeModels;

    GlobalThermalDomain globalDomain;
    bool globalTopologyDirtyFlag = false;
    bool globalValuesDirtyFlag = false;

    std::unique_ptr<HeatGlobalSolver> globalSolver;
    bool allGlobalFragmentsFixedFlag = false;
    std::vector<GlobalBoundaryRegion> globalBoundaryRegions;
    ComputePass::Synchronization globalSynchronization;
    bool globalSolveInFlight = false;
    bool pendingDestinationIsA = false;
    uint64_t cudaReadyCounter = 0;
    uint64_t vulkanReleaseCounter = 0;
    uint64_t pendingCudaReadyValue = 0;
    uint64_t lastVulkanReleaseValue[2]{0, 0};
};