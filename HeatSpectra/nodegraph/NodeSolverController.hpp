#pragma once

#include "NodeGraphDataTypes.hpp"
#include "contact/ContactTypes.hpp"
#include "heat/HeatContactParams.hpp"
#include "heat/HeatSolveParams.hpp"
#include "heat/HeatSystemPresets.hpp"

#include <cstdint>
#include <vector>

class HeatSystemController;
class ModelRegistry;

class NodeSolverController {
public:
    struct HeatSolveContactInput {
        NodeGraphSocketId inputSocketId{};
        ContactPairData contactPair;
        HeatContactParams params{};
    };

    NodeSolverController(
        ModelRegistry& modelRegistry,
        HeatSystemController& heatSystemController);

    void setHeatSolveModelRoles(
        const std::vector<uint32_t>& sourceNodeModelIds,
        const std::vector<uint32_t>& receiverNodeModelIds);
    void setHeatSolveContactPairs(const std::vector<HeatSolveContactInput>& contactPairs, bool forceContactRebuild = false);
    void setHeatSolveParams(const HeatSolveParams& params);
    void setHeatSolveMaterialBindings(
        const std::vector<GeometryData>& receiverGeometryInputs,
        const std::vector<HeatMaterialBindingEntry>& materialBindings);

    bool isHeatSolveActive() const;
    bool isHeatSolvePaused() const;
    bool deactivateHeatSolveIfActive();
    bool ensureHeatSolveRunningState(bool wantsPaused);
    void resetHeatSolve();

private:
    ModelRegistry& modelRegistry;
    HeatSystemController& heatSystemController;
};
