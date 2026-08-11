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
#include "domain/SerialTemperatureData.hpp"
#include "hash/HashValues.hpp"
#include "nodegraph/NodeGraphProductTypes.hpp"
#include "util/Units.hpp"

//                                                   [ Invariant:
//                                                     - Package types are the compiled runtime transport contract
//                                                       consumed by compute and display transports
//                                                     - Packages may combine authored/compiled settings with resolved
//                                                       runtime dependency handles needed by compute or display transports
//                                                     - Packages must not contain resolved runtime Products
//                                                     - Packages are the runtime application boundary ]

struct ModelPackage {
    HashValues hashes{};
    ProductHandle productHandle{};
    NodeDataHandle geometryHandle{};
    GeometryData sourceGeometry;
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    float canonicalToWorldScale = 1.0f;

    uint64_t computeHash() const { return hashes.geometry; }
    uint64_t displayHash() const { return hashes.display; }
    bool hasValidProduct() const { return productHandle.isValid(); }
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

    HashValues hashes{};
    ProductHandle productHandle{};
    GeometryData sourceGeometry;
    int iterations = 1;
    float minAngleDegrees = 20.0f;
    float maxEdgeLength = 0.1f;
    float stepSize = 0.25f;
    DisplaySettings display{};
    NodeDataHandle remeshHandle{};
    NodeDataHandle sourceMeshHandle{};
    ProductHandle sourceModelProduct{};
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    uint64_t computeHash() const { return hashes.geometry; }
    uint64_t displayHash() const { return hashes.display; }
    bool hasValidProduct() const { return productHandle.isValid(); }
};

struct VoronoiPackage {
    float canonicalToWorldScale = 1.0f;
    struct DisplaySettings {
        bool showVoronoi = false;
        bool showPoints = false;

        bool anyVisible() const {
            return showVoronoi || showPoints;
        }
    };

    HashValues hashes{};
    ProductHandle productHandle{};
    VoronoiData authored;
    NodeDataHandle voronoiHandle{};
    DisplaySettings display{};
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    DomainType domainType = DomainType::Mesh;
    NodeDataHandle modelMeshHandle{};
    NodeDataHandle modelRemeshHandle{};
    NodeDataHandle pointsPayloadHandle{};
    std::vector<glm::vec4> pointPositions;
    std::array<glm::vec3, 8> pointDomainCorners{};
    ProductHandle modelProduct{};
    ProductHandle remeshProduct{};

    uint64_t computeHash() const { return hashes.simulation; }
    uint64_t displayHash() const { return hashes.display; }
    bool hasValidProduct() const { return productHandle.isValid(); }
};

struct PointPackage {
    HashValues hashes{};
    ProductHandle productHandle{};
    NodeDataHandle pointsPayloadHandle{};
    std::vector<glm::vec4> positions;
    glm::vec3 domainMinimum{0.0f};
    glm::vec3 domainMaximum{0.0f};
    uint32_t pointCount = 0;
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    uint64_t computeHash() const { return hashes.geometry; }
    uint64_t displayHash() const { return hashes.display; }
    bool hasValidProduct() const { return productHandle.isValid(); }
};

struct HeatModelPackage {
    ProductHandle modelProduct{};
    ProductHandle remeshProduct{};
    ProductHandle voronoiProduct{};
    std::array<float, 16> localToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    float density = 0.0f;
    float specificHeat = 0.0f;
    float conductivity = 0.0f;
    float initialTemperatureC = 0.0f;

    uint32_t boundaryConditionType = 0;
    float boundaryTemperatureC = 0.0f;
    float boundaryHeatFlux = 0.0f;
    float boundaryHeatTransferCoefficient = 0.0f;
    float volumetricPowerDensity = 0.0f;
    uint64_t robinSourceKey = 0;
};

struct HeatPackage {
    struct DisplaySettings {
        bool showHeatOverlay = false;
        bool showFluxVectors = false;
        bool showHeatPalette = false;
        float fluxVectorScale = 1.0f;

        bool anyVisible() const {
            return showHeatOverlay || showFluxVectors || showHeatPalette;
        }
    };

    HashValues hashes{};
    ProductHandle productHandle{};
    HeatData authored;
    NodeDataHandle heatHandle{};
    units::LengthUnit worldUnit = units::defaultLengthUnit();
    float canonicalToWorldScale = 1.0f;
    DisplaySettings display{};

    std::vector<HeatModelPackage> models;
    std::vector<ProductHandle> contactProducts;
    std::unordered_map<uint64_t, SerialTemperatureData> resolvedSerialSources;

    uint64_t computeHash() const { return hashes.full; }
    uint64_t displayHash() const { return hashes.display; }
    bool hasValidProduct() const { return productHandle.isValid(); }
};

struct ContactPackage {
    struct DisplaySettings {
        bool showContactLines = false;

        bool anyVisible() const {
            return showContactLines;
        }
    };

    uint64_t computeHash() const { return hashes.simulation; }
    uint64_t displayHash() const { return hashes.display; }
    bool hasValidProduct() const { return productHandle.isValid(); }

    HashValues hashes{};
    ProductHandle productHandle{};
    ContactData authored;
    NodeDataHandle contactHandle{};
    DisplaySettings display{};
    std::array<float, 16> modelALocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::array<float, 16> modelBLocalToWorld{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    ProductHandle modelARemeshProduct{};
    ProductHandle modelBRemeshProduct{};
};
