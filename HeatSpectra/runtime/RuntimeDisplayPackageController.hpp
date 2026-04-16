#pragma once

#include <cstdint>
#include <unordered_map>

#include "runtime/RuntimeContactDisplayTransport.hpp"
#include "runtime/RuntimeHeatDisplayTransport.hpp"
#include "runtime/RuntimeRemeshDisplayTransport.hpp"
#include "runtime/RuntimeModelDisplayTransport.hpp"
#include "runtime/RuntimePackageGraph.hpp"
#include "runtime/RuntimeVoronoiDisplayTransport.hpp"

struct CompiledPackages;
struct PackagePlan;
struct PackagePlanGroup;

class RuntimeDisplayPackageController {
public:
    RuntimeDisplayPackageController() = default;

    void setModelTransport(RuntimeModelDisplayTransport* modelTransport);
    void setRemeshDisplayTransport(RuntimeRemeshDisplayTransport* remeshTransport);
    void setHeatDisplayTransport(RuntimeHeatDisplayTransport* heatTransport);
    void setContactDisplayTransport(RuntimeContactDisplayTransport* contactTransport);
    void setVoronoiDisplayTransport(RuntimeVoronoiDisplayTransport* voronoiTransport);
    void executeRemovals(const PackagePlan& plan);
    void executeGroup(const PackagePlanGroup& group, const CompiledPackages& compiledPackages);

private:
    void applyModel(uint64_t socketKey, const ModelPackage& modelPackage);
    void removeModel(uint64_t socketKey);
    void applyRemesh(uint64_t socketKey, const RemeshPackage& remeshPackage);
    void removeRemesh(uint64_t socketKey);
    void applyHeat(uint64_t socketKey, const HeatPackage& heatPackage);
    void removeHeat(uint64_t socketKey);
    void applyContact(uint64_t socketKey, const ContactPackage& contactPackage);
    void removeContact(uint64_t socketKey);
    void applyVoronoi(uint64_t socketKey, const VoronoiPackage& voronoiPackage);
    void removeVoronoi(uint64_t socketKey);
    void syncBackendSystems();

    std::unordered_map<uint64_t, ModelPackage> modelPackagesBySocket;
    std::unordered_map<uint64_t, RemeshPackage> remeshPackagesBySocket;
    std::unordered_map<uint64_t, HeatPackage> heatPackagesBySocket;
    std::unordered_map<uint64_t, ContactPackage> contactPackagesBySocket;
    std::unordered_map<uint64_t, VoronoiPackage> voronoiPackagesBySocket;
    bool modelSystemsDirty = false;
    bool remeshSystemsDirty = false;
    bool heatSystemsDirty = false;
    bool contactSystemsDirty = false;
    bool voronoiSystemsDirty = false;
    RuntimeModelDisplayTransport* modelTransport = nullptr;
    RuntimeRemeshDisplayTransport* remeshTransport = nullptr;
    RuntimeHeatDisplayTransport* heatTransport = nullptr;
    RuntimeContactDisplayTransport* contactTransport = nullptr;
    RuntimeVoronoiDisplayTransport* voronoiTransport = nullptr;
};
