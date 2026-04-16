#pragma once

#include <array>
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
#include "runtime/RuntimeThermalTypes.hpp"

//                                                   [ Invariant:
//                                                     - Package types are the compiled runtime transport contract
//                                                       consumed by runtime package sync/controllers
//                                                     - Packages may combine authored/compiled settings with resolved
//                                                       runtime dependency handles needed by compute or display transports
//                                                     - Packages must not contain resolved runtime Products
//                                                     - Packages are the runtime application boundary ]

struct ModelPackage {
    uint64_t packageHash = 0;
    GeometryData geometry;
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    bool matches(const ModelPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct RemeshPackage {
    struct DisplaySettings {
        bool showRemeshOverlay = false;
        bool showFaceNormals = false;
        bool showVertexNormals = false;
        float normalLength = 0.05f;

        bool anyVisible() const {
            return showRemeshOverlay || showFaceNormals || showVertexNormals;
        }
    };

    uint64_t packageHash = 0;
    uint64_t displayPackageHash = 0;
    GeometryData sourceGeometry;
    RemeshParams params{};
    DisplaySettings display{};
    NodeDataHandle remeshHandle{};
    ProductHandle modelProductHandle{};

    bool matches(const RemeshPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct VoronoiPackage {
    struct DisplaySettings {
        bool showVoronoi = false;
        bool showPoints = false;

        bool anyVisible() const {
            return showVoronoi || showPoints;
        }
    };

    uint64_t packageHash = 0;
    uint64_t displayPackageHash = 0;
    VoronoiData authored;
    DisplaySettings display{};
    std::vector<std::array<float, 16>> receiverLocalToWorlds;
    std::vector<ProductHandle> receiverModelProducts;
    std::vector<ProductHandle> receiverRemeshProducts;

    bool matches(const VoronoiPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct HeatPackage {
    struct DisplaySettings {
        bool showHeatOverlay = false;

        bool anyVisible() const {
            return showHeatOverlay;
        }
    };

    uint64_t packageHash = 0;
    uint64_t displayPackageHash = 0;
    HeatData authored;
    DisplaySettings display{};
    ProductHandle voronoiProduct{};
    ProductHandle contactProduct{};
    std::vector<ProductHandle> sourceModelProducts;
    std::vector<ProductHandle> sourceRemeshProducts;
    std::vector<float> sourceTemperatures;
    std::vector<ProductHandle> receiverModelProducts;
    std::vector<ProductHandle> receiverRemeshProducts;
    std::vector<RuntimeThermalMaterial> runtimeThermalMaterials;

    bool matches(const HeatPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct ContactPackage {
    struct DisplaySettings {
        bool showContactLines = false;

        bool anyVisible() const {
            return showContactLines;
        }
    };

    uint64_t packageHash = 0;
    uint64_t displayPackageHash = 0;
    ContactData authored;
    DisplaySettings display{};
    std::array<float, 16> emitterLocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::array<float, 16> receiverLocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    ProductHandle emitterModelProduct{};
    ProductHandle receiverModelProduct{};
    ProductHandle emitterRemeshProduct{};
    ProductHandle receiverRemeshProduct{};

    bool matches(const ContactPackage& other) const {
        return packageHash == other.packageHash;
    }
};

struct RuntimePackageSet {
    std::unordered_map<uint64_t, ModelPackage> modelBySocket;
    std::unordered_map<uint64_t, RemeshPackage> remeshBySocket;
    std::unordered_map<uint64_t, VoronoiPackage> voronoiBySocket;
    std::unordered_map<uint64_t, ContactPackage> contactBySocket;
    std::unordered_map<uint64_t, HeatPackage> heatBySocket;
};
