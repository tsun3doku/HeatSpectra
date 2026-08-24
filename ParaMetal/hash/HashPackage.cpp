#include "HashPackage.hpp"
#include "HashBuilder.hpp"

#include "runtime/package/RuntimePackages.hpp"

#include <algorithm>
#include <vector>

static void combineHandleHash(uint64_t& hash, const ProductHandle& handle, HashDomain domain) {
    HashBuilder::combine(hash, static_cast<uint64_t>(handle.type));
    HashBuilder::combine(hash, handle.outputSocketKey);
    HashBuilder::combine(hash, handle.hashes.get(domain));
}

void HashPackage::seal(ModelPackage& pkg, const HashValues& sourceGeometryHashes, const HashValues& outputHashes) {
    uint64_t geometryHash = HashBuilder::start();
    HashBuilder::combine(geometryHash, sourceGeometryHashes.geometry);
    HashBuilder::combine(geometryHash, pkg.geometryHandle.key);
    HashBuilder::combineFloat(geometryHash, pkg.canonicalToWorldScale);

    uint64_t fullHash = HashBuilder::start();
    HashBuilder::combine(fullHash, geometryHash);
    HashBuilder::combine(fullHash, outputHashes.full);
    HashBuilder::combinePod(fullHash, pkg.localToWorld);

    pkg.hashes.geometry = geometryHash;
    pkg.hashes.simulation = fullHash;
    pkg.hashes.full = fullHash;
    pkg.hashes.display = fullHash;
    pkg.hashes.thermal = 0;
}

void HashPackage::seal(RemeshPackage& pkg, const HashValues& sourceGeometryHashes) {
    uint64_t geometryHash = HashBuilder::start();
    HashBuilder::combine(geometryHash, sourceGeometryHashes.full);
    HashBuilder::combine(geometryHash, pkg.sourceMeshHandle.key);
    HashBuilder::combinePod(geometryHash, pkg.localToWorld);
    combineHandleHash(geometryHash, pkg.sourceModelProduct, HashDomain::Geometry);
    HashBuilder::combine(geometryHash, static_cast<uint64_t>(pkg.iterations));
    HashBuilder::combineFloat(geometryHash, pkg.minAngleDegrees);
    HashBuilder::combineFloat(geometryHash, pkg.maxEdgeLength);
    HashBuilder::combineFloat(geometryHash, pkg.stepSize);

    uint64_t displayHash = HashBuilder::start();
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showRemeshOverlay ? 1u : 0u));
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showFaceNormals ? 1u : 0u));
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showVertexNormals ? 1u : 0u));
    HashBuilder::combineFloat(displayHash, pkg.display.normalLength);

    uint64_t fullHash = HashBuilder::start();
    HashBuilder::combine(fullHash, geometryHash);
    HashBuilder::combine(fullHash, displayHash);

    pkg.hashes.geometry = geometryHash;
    pkg.hashes.simulation = geometryHash;
    pkg.hashes.full = fullHash;
    pkg.hashes.display = displayHash;
    pkg.hashes.thermal = 0;
}

void HashPackage::seal(VoronoiPackage& pkg, const HashValues& authoredHashes) {
    uint64_t geometryHash = HashBuilder::start();
    HashBuilder::combine(geometryHash, authoredHashes.full);

    HashBuilder::combine(geometryHash, static_cast<uint64_t>(pkg.domainType));
    HashBuilder::combineFloat(geometryHash, pkg.authored.cellSize);
    HashBuilder::combine(geometryHash, static_cast<uint64_t>(pkg.authored.voxelResolution));
    HashBuilder::combineFloat(geometryHash, pkg.authored.sdfPadding);
    HashBuilder::combinePod(geometryHash, pkg.localToWorld);
    HashBuilder::combine(geometryHash, pkg.pointsPayloadHandle.key);
    HashBuilder::combine(geometryHash, static_cast<uint64_t>(pkg.globalRemeshProducts.size()));
    for (const ProductHandle& handle : pkg.globalRemeshProducts) {
        combineHandleHash(geometryHash, handle, HashDomain::Geometry);
    }
    for (const ProductHandle& handle : pkg.globalModelProducts) {
        combineHandleHash(geometryHash, handle, HashDomain::Geometry);
    }
    HashBuilder::combinePodVector(geometryHash, pkg.pointPositions);
    HashBuilder::combineFloat(geometryHash, pkg.canonicalToWorldScale);

    uint64_t displayHash = HashBuilder::start();
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showVoronoi ? 1u : 0u));
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showPoints ? 1u : 0u));

    uint64_t fullHash = HashBuilder::start();
    HashBuilder::combine(fullHash, geometryHash);
    HashBuilder::combine(fullHash, displayHash);

    pkg.hashes.geometry = geometryHash;
    pkg.hashes.simulation = geometryHash;
    pkg.hashes.full = fullHash;
    pkg.hashes.display = displayHash;
    pkg.hashes.thermal = 0;
}

void HashPackage::seal(PointPackage& pkg, const HashValues& sourceHashes) {
    uint64_t geometryHash = HashBuilder::start();
    HashBuilder::combine(geometryHash, sourceHashes.geometry);
    HashBuilder::combine(geometryHash, pkg.pointsPayloadHandle.key);
    HashBuilder::combinePodVector(geometryHash, pkg.positions);
    HashBuilder::combine(geometryHash, static_cast<uint64_t>(pkg.pointCount));
    for (float v : pkg.localToWorld) {
        HashBuilder::combineFloat(geometryHash, v);
    }

    pkg.hashes.geometry = geometryHash;
    pkg.hashes.simulation = geometryHash;
    pkg.hashes.full = geometryHash;
    pkg.hashes.display = geometryHash;
    pkg.hashes.thermal = 0;
}

void HashPackage::seal(HeatPackage& pkg, const HashValues& authoredHashes) {
    uint64_t simulationHash = HashBuilder::start();
    HashBuilder::combine(simulationHash, authoredHashes.simulation);
    HashBuilder::combine(simulationHash, static_cast<uint64_t>(pkg.worldUnit));

    HashBuilder::combine(simulationHash, static_cast<uint64_t>(pkg.models.size()));
    for (const HeatModelPackage& model : pkg.models) {
        combineHandleHash(simulationHash, model.remeshProduct, HashDomain::Geometry);
        combineHandleHash(simulationHash, model.modelProduct, HashDomain::Geometry);
        HashBuilder::combineFloat(simulationHash, model.density);
        HashBuilder::combineFloat(simulationHash, model.specificHeat);
        HashBuilder::combineFloat(simulationHash, model.conductivity);
        HashBuilder::combineFloat(simulationHash, model.initialTemperatureC);
        HashBuilder::combine(simulationHash, static_cast<uint64_t>(model.boundaryConditionType));
        HashBuilder::combineFloat(simulationHash, model.boundaryTemperatureC);
        HashBuilder::combineFloat(simulationHash, model.boundaryHeatFlux);
        HashBuilder::combineFloat(simulationHash, model.boundaryHeatTransferCoefficient);
        HashBuilder::combineFloat(simulationHash, model.volumetricPowerDensity);
        HashBuilder::combine(simulationHash, model.robinSourceKey);
    }
    std::vector<uint64_t> serialKeys;
    serialKeys.reserve(pkg.resolvedSerialSources.size());
    for (const auto& [key, unused] : pkg.resolvedSerialSources) {
        (void)unused;
        serialKeys.push_back(key);
    }
    std::sort(serialKeys.begin(), serialKeys.end());
    for (uint64_t key : serialKeys) {
        const auto& source = pkg.resolvedSerialSources.at(key);
        HashBuilder::combine(simulationHash, key);
        HashBuilder::combine(simulationHash, source.enabled ? 1u : 0u);
        HashBuilder::combineString(simulationHash, source.portName);
        HashBuilder::combine(simulationHash, source.baudRate);
    }
    HashBuilder::combine(simulationHash, pkg.domainVoronoiHandle.key);
    combineHandleHash(simulationHash, pkg.domainVoronoiProduct, HashDomain::Geometry);

    uint64_t displayHash = HashBuilder::start();
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showHeatOverlay ? 1u : 0u));
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showFluxVectors ? 1u : 0u));
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showHeatPalette ? 1u : 0u));
    HashBuilder::combine(displayHash, static_cast<uint64_t>(pkg.display.showContactLevelSet ? 1u : 0u));
    HashBuilder::combineFloat(displayHash, pkg.display.fluxVectorScale);
    HashBuilder::combineFloat(displayHash, pkg.display.contactLevelSetRange);
    HashBuilder::combineFloat(displayHash, pkg.canonicalToWorldScale);
    for (const HeatModelPackage& model : pkg.models) {
        HashBuilder::combinePod(displayHash, model.localToWorld);
    }

    uint64_t fullHash = HashBuilder::start();
    HashBuilder::combine(fullHash, simulationHash);
    HashBuilder::combine(fullHash, displayHash);
    HashBuilder::combine(fullHash, authoredHashes.full);

    pkg.hashes.simulation = simulationHash;
    pkg.hashes.geometry = simulationHash;
    pkg.hashes.full = fullHash;
    pkg.hashes.display = displayHash;
}

