#include "NodeSolverController.hpp"

#include "NodePanelUtils.hpp"
#include "heat/HeatSystemController.hpp"
#include "scene/ModelRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

bool tryParseUint32(const std::string& value, uint32_t& outValue) {
    const std::string trimmed = NodePanelUtils::trimCopy(value);
    if (trimmed.empty()) {
        return false;
    }

    for (char character : trimmed) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return false;
        }
    }

    try {
        const unsigned long parsed = std::stoul(trimmed);
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        outValue = static_cast<uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool tryParseReceiverModelKey(const std::string& value, uint32_t& outModelId) {
    if (tryParseUint32(value, outModelId)) {
        return true;
    }

    const std::string trimmed = NodePanelUtils::trimCopy(value);
    constexpr const char* receiverPrefix = "receiver:";
    constexpr const char* modelPrefix = "model:";
    if (trimmed.rfind(receiverPrefix, 0) == 0) {
        return tryParseUint32(trimmed.substr(9), outModelId);
    }
    if (trimmed.rfind(modelPrefix, 0) == 0) {
        return tryParseUint32(trimmed.substr(6), outModelId);
    }

    return false;
}

} // namespace

NodeSolverController::NodeSolverController(
    ModelRegistry& modelRegistryRef,
    HeatSystemController& heatSystemControllerRef)
    : modelRegistry(modelRegistryRef),
      heatSystemController(heatSystemControllerRef) {
}

void NodeSolverController::setHeatSolveModelRoles(
    const std::vector<uint32_t>& sourceNodeModelIds,
    const std::vector<uint32_t>& receiverNodeModelIds) {
    std::vector<uint32_t> sourceRuntimeModelIds;
    std::vector<uint32_t> receiverRuntimeModelIds;
    std::unordered_set<uint32_t> seenSourceRuntimeModelIds;
    std::unordered_set<uint32_t> seenReceiverRuntimeModelIds;

    for (uint32_t nodeModelId : sourceNodeModelIds) {
        uint32_t runtimeModelId = 0;
        if (!modelRegistry.tryGetNodeModelRuntimeId(nodeModelId, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }

        if (seenSourceRuntimeModelIds.insert(runtimeModelId).second) {
            sourceRuntimeModelIds.push_back(runtimeModelId);
        }
    }

    for (uint32_t nodeModelId : receiverNodeModelIds) {
        uint32_t runtimeModelId = 0;
        if (!modelRegistry.tryGetNodeModelRuntimeId(nodeModelId, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }

        if (seenReceiverRuntimeModelIds.insert(runtimeModelId).second) {
            receiverRuntimeModelIds.push_back(runtimeModelId);
        }
    }

    std::sort(sourceRuntimeModelIds.begin(), sourceRuntimeModelIds.end());
    std::sort(receiverRuntimeModelIds.begin(), receiverRuntimeModelIds.end());

    heatSystemController.setActiveModels(sourceRuntimeModelIds, receiverRuntimeModelIds);
}

void NodeSolverController::setHeatSolveContactPairs(const std::vector<HeatSolveContactInput>& contactPairs, bool forceContactRebuild) {
    std::vector<HeatContactBinding> runtimeContactPairs;
    runtimeContactPairs.reserve(contactPairs.size());

    for (const HeatSolveContactInput& input : contactPairs) {
        const ContactPairData& contactPair = input.contactPair;
        if (!contactPair.hasValidContact || contactPair.emitterModelId == 0 || contactPair.receiverModelId == 0) {
            continue;
        }

        uint32_t emitterRuntimeModelId = 0;
        uint32_t receiverRuntimeModelId = 0;
        if (!modelRegistry.tryGetNodeModelRuntimeId(contactPair.emitterModelId, emitterRuntimeModelId) ||
            !modelRegistry.tryGetNodeModelRuntimeId(contactPair.receiverModelId, receiverRuntimeModelId) ||
            emitterRuntimeModelId == 0 || receiverRuntimeModelId == 0 ||
            emitterRuntimeModelId == receiverRuntimeModelId) {
            continue;
        }

        const ContactCouplingKind couplingKind =
            (contactPair.kind == ContactPairKind::ReceiverToReceiver)
            ? ContactCouplingKind::ReceiverToReceiver
            : ContactCouplingKind::SourceToReceiver;

        HeatContactBinding runtimeContactPair{};
        runtimeContactPair.pair.kind = couplingKind;
        runtimeContactPair.pair.emitterModelId = emitterRuntimeModelId;
        runtimeContactPair.pair.receiverModelId = receiverRuntimeModelId;
        runtimeContactPair.pair.minNormalDot = contactPair.minNormalDot;
        runtimeContactPair.pair.contactRadius = contactPair.contactRadius;
        runtimeContactPair.params = input.params;
        runtimeContactPairs.push_back(runtimeContactPair);
    }

    std::sort(
        runtimeContactPairs.begin(),
        runtimeContactPairs.end(),
        [](const HeatContactBinding& lhs, const HeatContactBinding& rhs) {
            return std::tie(
                    lhs.pair.kind,
                    lhs.pair.emitterModelId,
                    lhs.pair.receiverModelId,
                    lhs.pair.minNormalDot,
                    lhs.pair.contactRadius,
                    lhs.params.thermalConductance,
                    lhs.params.contactPressure,
                    lhs.params.frictionCoeff) <
                std::tie(
                    rhs.pair.kind,
                    rhs.pair.emitterModelId,
                    rhs.pair.receiverModelId,
                    rhs.pair.minNormalDot,
                    rhs.pair.contactRadius,
                    rhs.params.thermalConductance,
                    rhs.params.contactPressure,
                    rhs.params.frictionCoeff);
        });

    std::vector<uint32_t> sourceRuntimeModelIds;
    std::vector<uint32_t> receiverRuntimeModelIds;
    std::unordered_set<uint32_t> seenSourceRuntimeModelIds;
    std::unordered_set<uint32_t> seenReceiverRuntimeModelIds;
    for (const HeatContactBinding& runtimeContactPair : runtimeContactPairs) {
        if (runtimeContactPair.pair.kind == ContactCouplingKind::SourceToReceiver) {
            if (runtimeContactPair.pair.emitterModelId != 0 &&
                seenSourceRuntimeModelIds.insert(runtimeContactPair.pair.emitterModelId).second) {
                sourceRuntimeModelIds.push_back(runtimeContactPair.pair.emitterModelId);
            }
        } else {
            if (runtimeContactPair.pair.emitterModelId != 0 &&
                seenReceiverRuntimeModelIds.insert(runtimeContactPair.pair.emitterModelId).second) {
                receiverRuntimeModelIds.push_back(runtimeContactPair.pair.emitterModelId);
            }
        }

        if (runtimeContactPair.pair.receiverModelId != 0 &&
            seenReceiverRuntimeModelIds.insert(runtimeContactPair.pair.receiverModelId).second) {
            receiverRuntimeModelIds.push_back(runtimeContactPair.pair.receiverModelId);
        }
    }

    std::sort(sourceRuntimeModelIds.begin(), sourceRuntimeModelIds.end());
    std::sort(receiverRuntimeModelIds.begin(), receiverRuntimeModelIds.end());

    heatSystemController.setActiveModels(
        sourceRuntimeModelIds,
        receiverRuntimeModelIds,
        false);
    heatSystemController.setContactPairs(runtimeContactPairs, forceContactRebuild);
}

void NodeSolverController::setHeatSolveParams(const HeatSolveParams& params) {
    heatSystemController.setSolveParams(params);
}

void NodeSolverController::setHeatSolveMaterialBindings(
    const std::vector<GeometryData>& receiverGeometryInputs,
    const std::vector<HeatMaterialBindingEntry>& materialBindings) {
    std::unordered_map<uint32_t, HeatMaterialPresetId> presetByNodeModelId;
    std::optional<HeatMaterialPresetId> fallbackPreset;
    for (const HeatMaterialBindingEntry& binding : materialBindings) {
        uint32_t nodeModelId = 0;
        if (tryParseReceiverModelKey(binding.groupName, nodeModelId) && nodeModelId != 0) {
            presetByNodeModelId[nodeModelId] = binding.presetId;
        } else if (!fallbackPreset.has_value()) {
            fallbackPreset = binding.presetId;
        }
    }

    std::vector<HeatModelMaterialBindings> runtimeBindings;
    std::unordered_set<uint32_t> seenRuntimeModelIds;

    for (const GeometryData& geometry : receiverGeometryInputs) {
        if (geometry.modelId == 0) {
            continue;
        }

        uint32_t runtimeModelId = 0;
        if (!modelRegistry.tryGetNodeModelRuntimeId(geometry.modelId, runtimeModelId) || runtimeModelId == 0) {
            continue;
        }
        if (!seenRuntimeModelIds.insert(runtimeModelId).second) {
            continue;
        }

        HeatModelMaterialBindings modelBindings{};
        modelBindings.runtimeModelId = runtimeModelId;

        HeatMaterialPresetId presetId = HeatMaterialPresetId::Aluminum;
        const auto explicitIt = presetByNodeModelId.find(geometry.modelId);
        if (explicitIt != presetByNodeModelId.end()) {
            presetId = explicitIt->second;
        } else if (fallbackPreset.has_value()) {
            presetId = *fallbackPreset;
        }

        HeatMaterialBindingEntry receiverBinding{};
        receiverBinding.groupName = std::to_string(geometry.modelId);
        receiverBinding.presetId = presetId;
        modelBindings.bindings.push_back(std::move(receiverBinding));

        runtimeBindings.push_back(std::move(modelBindings));
    }

    heatSystemController.setMaterialBindings(runtimeBindings);
}

bool NodeSolverController::isHeatSolveActive() const {
    return heatSystemController.isHeatSystemActive();
}

bool NodeSolverController::isHeatSolvePaused() const {
    return heatSystemController.isHeatSystemPaused();
}

bool NodeSolverController::deactivateHeatSolveIfActive() {
    if (!heatSystemController.isHeatSystemActive()) {
        return false;
    }

    heatSystemController.toggleHeatSystem();
    return true;
}

bool NodeSolverController::ensureHeatSolveRunningState(bool wantsPaused) {
    bool isActive = heatSystemController.isHeatSystemActive();
    bool isPaused = heatSystemController.isHeatSystemPaused();
    bool changed = false;

    if (!isActive) {
        heatSystemController.toggleHeatSystem();
        isActive = heatSystemController.isHeatSystemActive();
        isPaused = heatSystemController.isHeatSystemPaused();
        changed = true;
    }

    if (!isActive) {
        return changed;
    }

    if (wantsPaused && !isPaused) {
        heatSystemController.pauseHeatSystem();
        return true;
    }

    if (!wantsPaused && isPaused) {
        heatSystemController.toggleHeatSystem();
        return true;
    }

    return changed;
}

void NodeSolverController::resetHeatSolve() {
    heatSystemController.resetHeatSystem();
}
