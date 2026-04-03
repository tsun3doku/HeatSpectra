#pragma once

#include <cstdint>
#include <utility>
#include <string>
#include <unordered_map>

#include "runtime/RuntimeContactTransport.hpp"
#include "runtime/RuntimeHeatTransport.hpp"
#include "runtime/RuntimeModelTransport.hpp"
#include "runtime/RuntimeRemeshTransport.hpp"
#include "runtime/RuntimeVoronoiTransport.hpp"

class ResourceManager;
class SceneController;
struct RuntimeSyncPlan;

class RuntimePackageController {
public:
    explicit RuntimePackageController(ResourceManager& resourceManager);

    void setRemeshTransport(RuntimeRemeshTransport* remeshTransport);
    void setHeatTransport(RuntimeHeatTransport* heatTransport);
    void setContactTransport(RuntimeContactTransport* contactTransport);
    void setVoronoiTransport(RuntimeVoronoiTransport* voronoiTransport);
    void setModelTransport(RuntimeModelTransport* modelTransport);
    void setSceneController(SceneController* sceneController);
    void applyGeometry(uint64_t socketKey, const GeometryPackage& geometryPackage);
    void removeGeometry(uint64_t socketKey);
    void applyRemesh(uint64_t socketKey, const RemeshPackage& remeshPackage);
    void removeRemesh(uint64_t socketKey);
    void applyHeat(uint64_t socketKey, const HeatPackage& heatPackage);
    void removeHeat(uint64_t socketKey);
    void applyContact(uint64_t socketKey, const ContactPackage& contactPackage);
    void removeContact(uint64_t socketKey);
    void applyVoronoi(uint64_t socketKey, const VoronoiPackage& voronoiPackage);
    void removeVoronoi(uint64_t socketKey);
    void executePlan(const RuntimeSyncPlan& plan);
    void syncBackendSystems();

private:
    template <typename TPackage>
    static const TPackage* selectPackage(const std::unordered_map<uint64_t, TPackage>& packagesBySocket);
    template <typename TPackage>
    static std::pair<uint64_t, const TPackage*> selectPackageEntry(const std::unordered_map<uint64_t, TPackage>& packagesBySocket);

    ResourceManager& resourceManager;
    std::unordered_map<uint64_t, uint32_t> geometryNodeModelIdBySocketKey;
    std::unordered_map<uint64_t, RemeshPackage> remeshPackagesBySocket;
    std::unordered_map<uint64_t, HeatPackage> heatPackagesBySocket;
    std::unordered_map<uint64_t, ContactPackage> contactPackagesBySocket;
    std::unordered_map<uint64_t, VoronoiPackage> voronoiPackagesBySocket;
    bool remeshSystemsDirty = false;
    bool contactSystemsDirty = false;
    bool heatSystemsDirty = false;
    bool voronoiSystemsDirty = false;
    RuntimeContactTransport* contactTransport = nullptr;
    RuntimeHeatTransport* heatTransport = nullptr;
    RuntimeModelTransport* modelTransport = nullptr;
    RuntimeRemeshTransport* remeshTransport = nullptr;
    RuntimeVoronoiTransport* voronoiTransport = nullptr;
    SceneController* sceneController = nullptr;
};

template <typename TPackage>
const TPackage* RuntimePackageController::selectPackage(const std::unordered_map<uint64_t, TPackage>& packagesBySocket) {
    return selectPackageEntry(packagesBySocket).second;
}

template <typename TPackage>
std::pair<uint64_t, const TPackage*> RuntimePackageController::selectPackageEntry(
    const std::unordered_map<uint64_t, TPackage>& packagesBySocket) {
    const TPackage* selectedPackage = nullptr;
    uint64_t selectedSocketKey = 0;
    for (const auto& [socketKey, package] : packagesBySocket) {
        if (!selectedPackage || socketKey < selectedSocketKey) {
            selectedPackage = &package;
            selectedSocketKey = socketKey;
        }
    }

    return { selectedSocketKey, selectedPackage };
}
