#include "RuntimeHeatComputeTransport.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "hash/HashBuilder.hpp"

#include <iostream>

ProductHandle RuntimeHeatComputeTransport::apply(uint64_t socketKey, const HeatPackage& package) {
    if (!controller || !products || socketKey == 0) {
        return {};
    }

    if (!package.authored.active) {
        remove(socketKey);
        return {};
    }

    HeatSystemComputeController::Config config{};
    if (!tryBuildConfig(socketKey, package, config)) {
        remove(socketKey);
        return {};
    }

    const uint64_t computeHash = package.hashes.simulation;
    config.computeHash = computeHash;

    controller->apply(socketKey, config);

    HeatProduct heatProduct{};
    if (!controller->buildProduct(socketKey, heatProduct)) {
        return {};
    }

    ProductHandle handle = products->publish<HeatProduct>(socketKey, heatProduct);
    return handle;
}

void RuntimeHeatComputeTransport::remove(uint64_t socketKey) {
    if (!controller || socketKey == 0) {
        return;
    }
    controller->remove(socketKey);
}

bool RuntimeHeatComputeTransport::tryBuildConfig(
    uint64_t socketKey,
    const HeatPackage& package,
    HeatSystemComputeController::Config& outConfig) const {

    if (package.models.empty()) {
        return false;
    }

    outConfig = {};
    outConfig.active = package.authored.active;
    outConfig.worldUnit = package.worldUnit;
    outConfig.syntheticDirichletTestEnabled = false;
    outConfig.contactThermalConductance = package.authored.contactThermalConductance;
    outConfig.simulationDuration = package.authored.simulationDuration;
    for (const auto& [sourceKey, data] : package.resolvedSerialSources) {
        outConfig.serialEnabledBySourceKey[sourceKey] = data.enabled;
        outConfig.serialPortNamesBySourceKey[sourceKey] = data.portName;
        outConfig.serialBaudRatesBySourceKey[sourceKey] = data.baudRate;
    }

    const VoronoiProduct* domainProduct = products->resolve<VoronoiProduct>(
        package.domainVoronoiProduct);
    if (!domainProduct) {
        return false;
    }
    if (!domainProduct->isGlobalDomain || !domainProduct->isValid()) {
        return false;
    }

    HeatDomainRuntime::GlobalThermalDomain& globalDomain = outConfig.domainVoronoiProduct;
    globalDomain.fragments.reserve(domainProduct->globalFragmentCount);
    for (uint32_t fragmentId = 0; fragmentId < domainProduct->globalFragmentCount; ++fragmentId) {
        HeatDomainRuntime::GlobalFragment fragment{};
        fragment.instanceId = domainProduct->fragmentInstanceIds[fragmentId];
        fragment.seedId = domainProduct->fragmentSeedIds[fragmentId];
        fragment.materialId = fragment.instanceId; // Initial default: 1 material per model instance
        fragment.surfaceBoundaryArea = domainProduct->fragmentSurfaceBoundaryAreas[fragmentId];
        fragment.volume = domainProduct->fragmentVolumes[fragmentId];
        globalDomain.fragments.push_back(fragment);
    }
    globalDomain.faces.reserve(domainProduct->globalFaceCount);
    for (uint32_t faceId = 0; faceId < domainProduct->globalFaceCount; ++faceId) {
        HeatDomainRuntime::GlobalFace face{};
        face.instanceId = domainProduct->faceInstanceIds[faceId];
        face.fragmentA = domainProduct->faceFragmentA[faceId];
        face.fragmentB = domainProduct->faceFragmentB[faceId];
        face.area = domainProduct->faceAreas[faceId];
        globalDomain.faces.push_back(face);
    }
    globalDomain.cutFaces.reserve(domainProduct->globalCutFaceCount);
    for (uint32_t cutFaceId = 0; cutFaceId < domainProduct->globalCutFaceCount; ++cutFaceId) {
        HeatDomainRuntime::GlobalCutFace cutFace{};
        cutFace.fragmentA = domainProduct->cutFaceFragmentA[cutFaceId];
        cutFace.fragmentB = domainProduct->cutFaceFragmentB[cutFaceId];
        cutFace.area = domainProduct->cutFaceAreas[cutFaceId];
        cutFace.gap = domainProduct->cutFaceGaps[cutFaceId];
        globalDomain.cutFaces.push_back(cutFace);
    }
    globalDomain.instanceFragmentCounts = domainProduct->instanceFragmentCounts;
    globalDomain.seedPositions.reserve(domainProduct->globalSeedPositions.size());
    for (const glm::vec4& position : domainProduct->globalSeedPositions) {
        globalDomain.seedPositions.push_back(glm::vec3(position));
    }
    globalDomain.domainCorners = domainProduct->globalDomainCorners[0];
    globalDomain.sdfGridMin = domainProduct->globalSdfGridMin;
    globalDomain.sdfGridDim = domainProduct->globalSdfGridDim;
    globalDomain.sdfCellSize = domainProduct->globalSdfCellSize;
    globalDomain.sdfValues = domainProduct->globalSdfValues;
    globalDomain.sdfRuntimeModelIds = domainProduct->globalSdfRuntimeModelIds;

    const size_t modelCount = package.models.size();
    outConfig.modelSurfacePositions.reserve(modelCount);
    outConfig.modelSurfaceNormals.reserve(modelCount);
    outConfig.modelSurfaceTriangleIndices.reserve(modelCount);
    outConfig.modelRuntimeModelIds.reserve(modelCount);
    outConfig.modelInitialTemperaturesCByRuntimeId.reserve(modelCount);
    outConfig.modelBoundaryConditionTypesByRuntimeId.reserve(modelCount);
    outConfig.modelBoundaryTemperaturesCByRuntimeId.reserve(modelCount);
    outConfig.modelBoundaryHeatFluxesByRuntimeId.reserve(modelCount);
    outConfig.modelBoundaryHeatTransferCoefficientsByRuntimeId.reserve(modelCount);
    outConfig.modelVolumetricPowerDensitiesByRuntimeId.reserve(modelCount);
    outConfig.supportingHalfedgeViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.supportingAngleViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.halfedgeViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.edgeViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.triangleViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.lengthViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.inputHalfedgeViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.inputEdgeViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.inputTriangleViews.resize(modelCount, VK_NULL_HANDLE);
    outConfig.inputLengthViews.resize(modelCount, VK_NULL_HANDLE);

    for (size_t i = 0; i < modelCount; ++i) {
        const HeatModelPackage& model = package.models[i];
        const RemeshProduct* remeshProduct = products->resolve<RemeshProduct>(
            model.remeshProduct);
        if (!remeshProduct ||
            remeshProduct->runtimeModelId == 0) {
            return false;
        }

        const uint32_t runtimeModelId = remeshProduct->runtimeModelId;
        outConfig.modelSurfacePositions.push_back(remeshProduct->surfacePositions);
        outConfig.modelSurfaceNormals.push_back(remeshProduct->surfaceNormals);
        outConfig.modelSurfaceTriangleIndices.push_back(remeshProduct->surfaceTriangleIndices);
        outConfig.modelRuntimeModelIds.push_back(runtimeModelId);
        outConfig.modelInitialTemperaturesCByRuntimeId[runtimeModelId] = model.initialTemperatureC;
        outConfig.modelBoundaryConditionTypesByRuntimeId[runtimeModelId] = model.boundaryConditionType;
        outConfig.modelBoundaryTemperaturesCByRuntimeId[runtimeModelId] = model.boundaryTemperatureC;
        outConfig.modelBoundaryHeatFluxesByRuntimeId[runtimeModelId] = model.boundaryHeatFlux;
        outConfig.modelBoundaryHeatTransferCoefficientsByRuntimeId[runtimeModelId] =
            model.boundaryHeatTransferCoefficient;
        outConfig.modelVolumetricPowerDensitiesByRuntimeId[runtimeModelId] =
            model.volumetricPowerDensity;
        outConfig.modelDensity[runtimeModelId] = model.density;
        outConfig.modelSpecificHeat[runtimeModelId] = model.specificHeat;
        outConfig.modelConductivity[runtimeModelId] = model.conductivity;
        outConfig.modelLocalToWorldByModelId[runtimeModelId] = model.remeshLocalToWorld;
        if (model.robinSourceKey != 0) {
            outConfig.modelRobinSourceKeys[runtimeModelId] = model.robinSourceKey;
        }

        outConfig.supportingHalfedgeViews[i] = remeshProduct->supportingHalfedgeView;
        outConfig.supportingAngleViews[i] = remeshProduct->supportingAngleView;
        outConfig.halfedgeViews[i] = remeshProduct->halfedgeView;
        outConfig.edgeViews[i] = remeshProduct->edgeView;
        outConfig.triangleViews[i] = remeshProduct->triangleView;
        outConfig.lengthViews[i] = remeshProduct->lengthView;
        outConfig.inputHalfedgeViews[i] = remeshProduct->inputHalfedgeView;
        outConfig.inputEdgeViews[i] = remeshProduct->inputEdgeView;
        outConfig.inputTriangleViews[i] = remeshProduct->inputTriangleView;
        outConfig.inputLengthViews[i] = remeshProduct->inputLengthView;
    }

    uint64_t structuralHash = HashBuilder::start();
    HashBuilder::combine(structuralHash, static_cast<uint64_t>(package.models.size()));
    for (const HeatModelPackage& model : package.models) {
        HashBuilder::combine(structuralHash, model.remeshProduct.hashes.geometry);
        HashBuilder::combine(structuralHash, model.modelProduct.hashes.geometry);
        HashBuilder::combineFloat(structuralHash, model.density);
        HashBuilder::combineFloat(structuralHash, model.specificHeat);
        HashBuilder::combineFloat(structuralHash, model.conductivity);
        HashBuilder::combineFloat(structuralHash, model.initialTemperatureC);
        HashBuilder::combine(structuralHash, model.boundaryConditionType);
    }
    HashBuilder::combine(structuralHash, domainProduct->hashes.geometry);
    HashBuilder::combineFloat(structuralHash, package.authored.contactThermalConductance);
    HashBuilder::combineFloat(structuralHash, package.authored.simulationDuration);
    outConfig.structuralHash = structuralHash;

    uint64_t authoredSimulationHash = structuralHash;
    for (uint32_t runtimeModelId : outConfig.modelRuntimeModelIds) {
        HashBuilder::combine(authoredSimulationHash, runtimeModelId);
        HashBuilder::combineFloat(authoredSimulationHash, outConfig.modelBoundaryTemperaturesCByRuntimeId.at(runtimeModelId));
        HashBuilder::combineFloat(authoredSimulationHash, outConfig.modelBoundaryHeatFluxesByRuntimeId.at(runtimeModelId));
        HashBuilder::combineFloat(authoredSimulationHash, outConfig.modelBoundaryHeatTransferCoefficientsByRuntimeId.at(runtimeModelId));
        HashBuilder::combineFloat(authoredSimulationHash, outConfig.modelVolumetricPowerDensitiesByRuntimeId.at(runtimeModelId));
    }
    outConfig.authoredSimulationHash = authoredSimulationHash;

    return true;
}
