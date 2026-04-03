#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "contact/ContactTypes.hpp"
#include "domain/GeometryData.hpp"
#include "domain/ContactData.hpp"
#include "domain/HeatData.hpp"
#include "domain/RemeshData.hpp"
#include "domain/VoronoiData.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "nodegraph/NodeGraphProductTypes.hpp"
#include "runtime/RuntimeContactTypes.hpp"
#include "runtime/RuntimeThermalTypes.hpp"

//                                                          [ Invariant:
//                                                            - *Package types exist for backend sinks only
//                                                            - Packages hold authored config and ProductHandle dependencies
//                                                            - Packages are compiled from authored graph data into backend dependencies
//                                                            - Packages should not duplicate runtime data that Products already provide ]

struct GeometryPackage {
    uint64_t packageHash = 0;
    GeometryData geometry;
    ProductHandle modelProduct{};

    bool matches(const GeometryPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct RemeshPackage {
    uint64_t packageHash = 0;
    GeometryData sourceGeometry;
    RemeshParams params{};
    NodeDataHandle remeshHandle{};

    bool matches(const RemeshPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct VoronoiPackage {
    uint64_t packageHash = 0;
    VoronoiData authored;
    std::vector<ProductHandle> receiverModelProducts;
    std::vector<ProductHandle> receiverRemeshProducts;

    bool matches(const VoronoiPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct HeatPackage {
    uint64_t packageHash = 0;
    HeatData authored;
    ProductHandle voronoiProduct{};
    ProductHandle contactProduct{};
    std::vector<ProductHandle> sourceRemeshProducts;
    std::vector<float> sourceTemperatures;
    std::vector<ProductHandle> receiverRemeshProducts;
    std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;

    bool matches(const HeatPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct ContactPackage {
    uint64_t packageHash = 0;
    ContactData authored;
    ProductHandle emitterRemeshProduct{};
    ProductHandle receiverRemeshProduct{};

    bool matches(const ContactPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct RuntimePackageSet {
    std::unordered_map<uint64_t, GeometryPackage> geometryBySocket;
    std::unordered_map<uint64_t, RemeshPackage> remeshBySocket;
    std::unordered_map<uint64_t, VoronoiPackage> voronoiBySocket;
    std::unordered_map<uint64_t, ContactPackage> contactBySocket;
    std::unordered_map<uint64_t, HeatPackage> heatBySocket;
};
