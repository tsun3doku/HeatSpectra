#pragma once

#include <cstdint>
#include <unordered_map>

#include "runtime/RuntimeContactComputeTransport.hpp"
#include "runtime/RuntimeHeatComputeTransport.hpp"
#include "runtime/RuntimeModelComputeTransport.hpp"
#include "runtime/RuntimePackageGraph.hpp"
#include "runtime/RuntimeRemeshComputeTransport.hpp"
#include "runtime/RuntimeVoronoiComputeTransport.hpp"

struct CompiledPackages;
struct PackagePlan;
struct PackagePlanGroup;

class RuntimeComputePackageController {
public:
    RuntimeComputePackageController() = default;

    void setModelTransport(RuntimeModelComputeTransport* modelTransport);
    void setRemeshTransport(RuntimeRemeshComputeTransport* remeshTransport);
    void setHeatTransport(RuntimeHeatComputeTransport* heatTransport);
    void setContactComputeTransport(RuntimeContactComputeTransport* contactComputeTransport);
    void setVoronoiComputeTransport(RuntimeVoronoiComputeTransport* voronoiComputeTransport);
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
    bool contactSystemsDirty = false;
    bool heatSystemsDirty = false;
    bool voronoiSystemsDirty = false;
    RuntimeModelComputeTransport* modelTransport = nullptr;
    RuntimeContactComputeTransport* contactComputeTransport = nullptr;
    RuntimeHeatComputeTransport* heatTransport = nullptr;
    RuntimeRemeshComputeTransport* remeshTransport = nullptr;
    RuntimeVoronoiComputeTransport* voronoiComputeTransport = nullptr;
};
