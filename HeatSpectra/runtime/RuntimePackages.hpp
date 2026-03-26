#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "contact/ContactTypes.hpp"
#include "domain/GeometryData.hpp"
#include "domain/ContactData.hpp"
#include "domain/HeatData.hpp"
#include "domain/VoronoiData.hpp"
#include "runtime/RuntimeContactTypes.hpp"
#include "runtime/RuntimeThermalTypes.hpp"

//                                                          [ Invariant
//                                                            - A package holds everything the backend 
//                                                              needs from the graph, fully resolved 
//                                                            - It does not hold everything the backend 
//                                                              needs to operate ]

struct GeometryPackage {
    GeometryData geometry;
    uint32_t runtimeModelId = 0;
};

struct VoronoiPackage {
    VoronoiData authored;
    std::vector<NodeDataHandle> receiverGeometryHandles;
    std::vector<GeometryData> receiverGeometries;
    std::vector<IntrinsicMeshData> receiverIntrinsics;
    std::vector<uint32_t> receiverRuntimeModelIds;
};

struct HeatPackage {
    HeatData authored;
    bool voronoiActive = false;
    VoronoiParams voronoiParams{};
    std::vector<GeometryData> sourceGeometries;
    std::vector<IntrinsicMeshData> sourceIntrinsics;
    std::vector<uint32_t> sourceRuntimeModelIds;
    std::vector<NodeDataHandle> receiverGeometryHandles;
    std::vector<GeometryData> receiverGeometries;
    std::vector<IntrinsicMeshData> receiverIntrinsics;
    std::vector<uint32_t> receiverRuntimeModelIds;
    std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;
    std::unordered_map<uint32_t, float> sourceTemperatureByRuntimeId;
};

struct ContactPackage {
    ContactData authored;
    std::vector<RuntimeContactBinding> runtimeContactPairs;
    std::vector<uint32_t> sourceRuntimeModelIds;
    std::vector<GeometryData> sourceGeometries;
    std::vector<IntrinsicMeshData> sourceIntrinsics;
};
