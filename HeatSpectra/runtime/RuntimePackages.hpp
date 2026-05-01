#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "contact/ContactTypes.hpp"
#include "domain/GeometryData.hpp"
#include "domain/ContactData.hpp"
#include "domain/HeatData.hpp"
#include "domain/RemeshData.hpp"
#include "domain/VoronoiData.hpp"
#include "mesh/remesher/SupportingHalfedge.hpp"
#include "nodegraph/NodeGraphProductTypes.hpp"

//                                                   [ Invariant:
//                                                     - Package types are the compiled runtime transport contract
//                                                       consumed by compute and display transports
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
    GeometryData sourceGeometry;
    int iterations = 1;
    float minAngleDegrees = 20.0f;
    float maxEdgeLength = 0.1f;
    float stepSize = 0.25f;
    DisplaySettings display{};
    NodeDataHandle remeshHandle{};
    ProductHandle modelProductHandle{};

    bool matches(const RemeshPackage& other) const {
        return packageHash == other.packageHash &&
            display.showRemeshOverlay == other.display.showRemeshOverlay &&
            display.showFaceNormals == other.display.showFaceNormals &&
            display.showVertexNormals == other.display.showVertexNormals &&
            display.normalLength == other.display.normalLength;
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
    VoronoiData authored;
    DisplaySettings display{};
    std::vector<std::array<float, 16>> receiverLocalToWorlds;
    std::vector<ProductHandle> receiverModelProducts;
    std::vector<ProductHandle> receiverRemeshProducts;

    bool matches(const VoronoiPackage& other) const {
        return packageHash == other.packageHash &&
            display.showVoronoi == other.display.showVoronoi &&
            display.showPoints == other.display.showPoints;
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
    HeatData authored;
    DisplaySettings display{};
    ProductHandle voronoiProduct{};
    ProductHandle contactProduct{};
    std::vector<ProductHandle> sourceModelProducts;
    std::vector<ProductHandle> sourceRemeshProducts;
    std::vector<float> sourceTemperatures;
    std::vector<ProductHandle> receiverModelProducts;
    std::vector<ProductHandle> receiverRemeshProducts;

    bool matches(const HeatPackage& other) const {
        return packageHash == other.packageHash &&
            display.showHeatOverlay == other.display.showHeatOverlay;
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
        return packageHash == other.packageHash &&
            display.showContactLines == other.display.showContactLines;
    }
};
