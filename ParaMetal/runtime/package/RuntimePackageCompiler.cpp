#include "RuntimePackageCompiler.hpp"

#include "domain/HeatModelData.hpp"
#include "domain/PointData.hpp"
#include "hash/HashPackage.hpp"
#include "nodegraph/NodeGraphEvaluation.hpp"
#include "nodegraph/NodeGraphPayloadTypes.hpp"
#include "nodegraph/NodeGraphRegistry.hpp"
#include "nodegraph/NodeGraphState.hpp"
#include "nodegraph/NodeGraphUtils.hpp"
#include "nodegraph/NodeHeatSolveParams.hpp"
#include "nodegraph/NodePayloadRegistry.hpp"
#include "nodegraph/NodeRemeshParams.hpp"
#include "nodegraph/NodeTransform.hpp"
#include "nodegraph/NodeVoronoiParams.hpp"
#include "runtime/RuntimeProductManager.hpp"
#include "runtime/RuntimeProducts.hpp"
#include "runtime/package/RuntimePackageManager.hpp"
#include "runtime/package/passes/CoordinatePass.hpp"
#include "runtime/package/passes/ValidationPass.hpp"
#include "util/GeometryUtils.hpp"

#include <unordered_map>
#include <unordered_set>
#include <limits>

bool RuntimePackageCompiler::validate(
    const NodeGraphState& graph,
    const NodeGraphCompiled& graphPlan,
    const NodeGraphEvaluation& evaluation,
    const NodePayloadRegistry& payloads,
    std::vector<std::string>& errors) const {
    errors.clear();
    const runtimepackage::ValidationPass validationPass{};
    return validationPass.run(graph, graphPlan, evaluation, payloads, errors);
}

HashValues RuntimePackageCompiler::resolveHandleHashes(
    const NodePayloadRegistry& payloads,
    const NodeDataHandle& handle) {
    if (handle.key == 0) return {};
    HashValues values{};
    values.full = payloads.resolveHash(handle, HashDomain::Full);
    values.geometry = payloads.resolveHash(handle, HashDomain::Geometry);
    values.thermal = payloads.resolveHash(handle, HashDomain::Thermal);
    values.simulation = payloads.resolveHash(handle, HashDomain::Simulation);
    values.display = payloads.resolveHash(handle, HashDomain::Display);
    return values;
}

bool RuntimePackageCompiler::compileNode(
    const NodeGraphState& graph,
    const NodeGraphNode& node,
    const NodeGraphEvaluation& evaluation,
    const NodePayloadRegistry& payloads,
    const RuntimeProductManager& products,
    units::LengthUnit worldUnit,
    FrozenPackages frozenPackages,
    RuntimePackageManager& packages,
    std::vector<std::string>& errors) const {
    const NodeTypeId typeId = getNodeTypeId(node.typeId);
    for (const NodeGraphSocket& outputSocket : node.outputs) {
        const uint64_t outputSocketKey = NodeSocketKey(node.id, outputSocket.id).value;
        const EvaluatedSocketValue* evaluated = evaluation.outputFor(outputSocketKey);
        if (!evaluated || evaluated->status != EvaluatedSocketStatus::Value ||
            evaluated->data.payloadHandle.key == 0) {
            continue;
        }

        const NodeDataBlock& output = evaluated->data;
        if (output.isFrozen && frozenPackages == FrozenPackages::Preserve) {
            packages.retain(outputSocketKey);
            continue;
        }

        bool compiled = true;
        if (typeId == nodegraphtypes::Remesh)
            compiled = compileRemeshPackage(node, outputSocketKey, output, payloads, worldUnit, packages, errors);
        else if (typeId == nodegraphtypes::Voronoi)
            compiled = compileVoronoiPackage(node, outputSocketKey, output, payloads, worldUnit, packages, errors);
        else if (typeId == nodegraphtypes::HeatSolve)
            compiled = compileHeatPackage(node, outputSocketKey, output, payloads, worldUnit, packages, errors);
        else if (output.dataType == payloadtypes::Points)
            compiled = compilePointPackage(graph, node, outputSocketKey, output, payloads, products, worldUnit, packages, errors);
        else if (output.dataType == payloadtypes::Geometry)
            compiled = compileModelPackage(outputSocketKey, output, payloads, worldUnit, packages);
        if (!compiled) return false;
    }

    return true;
}

bool RuntimePackageCompiler::compileModelPackage(
    uint64_t outputSocketKey,
    const NodeDataBlock& output,
    const NodePayloadRegistry& payloads,
    units::LengthUnit worldUnit,
    RuntimePackageManager& packages) const {
    NodeDataHandle sourceGeometryHandle{};
    const GeometryData* geometry = payloads.resolveGeometry(
        output.payloadHandle, &sourceGeometryHandle);
    if (!geometry || sourceGeometryHandle.key == 0 ||
        geometry->pointPositions.empty() || geometry->triangleIndices.empty()) return true;
    const HashValues* sourceHashes = payloads.findHashes(sourceGeometryHandle);
    if (!sourceHashes || sourceHashes->geometry == 0) return true;
    ModelPackage package{};
    const ModelPackage* previous = packages.findStored<ModelPackage>(outputSocketKey);
    package.productHandle = previous ? previous->productHandle : ProductHandle{};
    package.geometryHandle = sourceGeometryHandle;
    if (!payloads.resolveLocalToWorld(output.payloadHandle, package.localToWorld)) return true;
    runtimepackage::CoordinatePass{}.run(package, worldUnit);
    HashPackage::seal(package, *sourceHashes, output.hashes);
    const bool computeChanged =
        !previous || !previous->productHandle.isValid() ||
        previous->computeHash() != package.computeHash();
    if (computeChanged) {
        package.sourceGeometry = *geometry;
        runtimepackage::CoordinatePass::scalePositions(
            package.sourceGeometry.pointPositions, package.canonicalToWorldScale);
        runtimepackage::CoordinatePass::scalePositions(
            package.sourceGeometry.renderPositions, package.canonicalToWorldScale);
    }
    packages.apply<ModelPackage>(outputSocketKey, package);
    return true;
}

bool RuntimePackageCompiler::compileRemeshPackage(
    const NodeGraphNode& node,
    uint64_t outputSocketKey,
    const NodeDataBlock& output,
    const NodePayloadRegistry& payloads,
    units::LengthUnit worldUnit,
    RuntimePackageManager& packages,
    std::vector<std::string>& errors) const {
    const RemeshData* remesh = payloads.get<RemeshData>(output.payloadHandle);
    if (!remesh || !remesh->active || remesh->sourceMeshHandle.key == 0) return true;
    const GeometryData* geometry = payloads.resolveGeometry(remesh->sourceMeshHandle);
    const ModelPackage* model = packages.findAny<ModelPackage>(remesh->sourceMeshHandle.key);
    if (!geometry || geometry->pointPositions.empty() || geometry->triangleIndices.empty() ||
        !model || !model->productHandle.isValid()) {
        errors.push_back("Remesh package is missing its upstream model product.");
        return false;
    }

    const RemeshNodeParams params = readRemeshNodeParams(node);
    RemeshPackage package{};
    const RemeshPackage* previous = packages.findStored<RemeshPackage>(outputSocketKey);
    package.productHandle = previous ? previous->productHandle : ProductHandle{};
    package.sourceGeometry = *geometry;
    if (!payloads.resolveLocalToWorld(remesh->sourceMeshHandle, package.localToWorld)) {
        errors.push_back("Remesh package cannot resolve source placement.");
        return false;
    }
    package.iterations = remesh->iterations;
    package.minAngleDegrees = remesh->minAngleDegrees;
    package.maxEdgeLength = remesh->maxEdgeLength;
    package.stepSize = remesh->stepSize;
    package.display.showRemeshOverlay = params.preview.showRemeshOverlay;
    package.display.showFaceNormals = params.preview.showFaceNormals;
    package.display.showVertexNormals = params.preview.showVertexNormals;
    package.display.normalLength = static_cast<float>(params.normalLength);
    package.remeshHandle = output.payloadHandle;
    package.sourceMeshHandle = remesh->sourceMeshHandle;
    package.sourceModelProduct = model->productHandle;
    runtimepackage::CoordinatePass{}.run(package, worldUnit);
    HashPackage::seal(package, resolveHandleHashes(payloads, remesh->sourceMeshHandle));
    packages.apply<RemeshPackage>(outputSocketKey, package);
    return true;
}

bool RuntimePackageCompiler::compileVoronoiPackage(
    const NodeGraphNode& node,
    uint64_t outputSocketKey,
    const NodeDataBlock& output,
    const NodePayloadRegistry& payloads,
    units::LengthUnit worldUnit,
    RuntimePackageManager& packages,
    std::vector<std::string>& errors) const {
    const VoronoiData* voronoi = payloads.get<VoronoiData>(output.payloadHandle);
    if (!voronoi || !voronoi->active) return true;
    const PointPackage* points = packages.findAny<PointPackage>(voronoi->pointsPayloadHandle.key);
    if (!points || points->positions.empty()) {
        errors.push_back("Voronoi package is missing its upstream point package.");
        return false;
    }

    VoronoiPackage package{};
    const VoronoiPackage* previous = packages.findStored<VoronoiPackage>(outputSocketKey);
    package.productHandle = previous ? previous->productHandle : ProductHandle{};
    package.authored = *voronoi;
    package.voronoiHandle = output.payloadHandle;
    package.domainType = voronoi->domainType;
    package.pointsPayloadHandle = voronoi->pointsPayloadHandle;
    const VoronoiNodeParams params = readVoronoiNodeParams(node);
    package.display.showVoronoi = params.preview.showVoronoi;
    package.display.showPoints = params.preview.showPoints;

    if (package.domainType == DomainType::Global) {
        if (voronoi->modelMeshHandles.empty()) {
            errors.push_back("Voronoi Global domain requires at least one unique Remeshes input.");
            return false;
        }
        std::unordered_set<uint64_t> seenRemeshKeys;
        for (const NodeDataHandle& remeshHandle : voronoi->modelMeshHandles) {
            if (remeshHandle.key == 0 ||
                !seenRemeshKeys.insert(remeshHandle.key).second) {
                errors.push_back("Voronoi Global domain contains an empty or duplicate remesh payload.");
                return false;
            }
            const RemeshPackage* remesh = packages.findAny<RemeshPackage>(remeshHandle.key);
            const ModelPackage* model = remesh
                ? packages.findAny<ModelPackage>(remesh->sourceMeshHandle.key)
                : nullptr;
            if (!remesh || !model ||
                !remesh->productHandle.isValid() || !model->productHandle.isValid()) {
                errors.push_back("Voronoi Global domain is missing an upstream remesh product.");
                return false;
            }
            package.globalRemeshHandles.push_back(remeshHandle);
            package.globalRemeshProducts.push_back(remesh->productHandle);
            package.globalModelProducts.push_back(model->productHandle);
        }
    }

    runtimepackage::CoordinatePass{}.run(package, worldUnit);
    const glm::mat4 pointTransform = toMat4(points->localToWorld);
    package.pointPositions = points->positions;
    for (glm::vec4& position : package.pointPositions) {
        position = pointTransform * position;
    }

    glm::vec3 worldDomainMin(std::numeric_limits<float>::max());
    glm::vec3 worldDomainMax(-std::numeric_limits<float>::max());
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const glm::vec3 localCorner(
            (corner & 1u) ? points->domainMaximum.x : points->domainMinimum.x,
            (corner & 2u) ? points->domainMaximum.y : points->domainMinimum.y,
            (corner & 4u) ? points->domainMaximum.z : points->domainMinimum.z);
        const glm::vec3 worldCorner = glm::vec3(pointTransform * glm::vec4(localCorner, 1.0f));
        package.pointDomainCorners[corner] = worldCorner;
        worldDomainMin = glm::min(worldDomainMin, worldCorner);
        worldDomainMax = glm::max(worldDomainMax, worldCorner);
    }

    if (package.domainType == DomainType::Global) {
        if (package.authored.voxelResolution < 2) {
            errors.push_back("Voronoi Global domain requires voxelResolution >= 2.");
            return false;
        }
        const glm::vec3 physicalExtent = worldDomainMax - worldDomainMin;
        const float longestExtent = std::max(std::max(physicalExtent.x, physicalExtent.y), physicalExtent.z);
        const float spacing = longestExtent /
            static_cast<float>(package.authored.voxelResolution - 1);
        if (spacing > 0.5f * package.authored.cellSize) {
            errors.push_back(
                "Voronoi Global domain violates the resolution rule: spacing (" +
                std::to_string(spacing) +
                ") must be <= 0.5 * cell size (" +
                std::to_string(0.5f * package.authored.cellSize) + ").");
            return false;
        }

        const glm::mat4 worldToPoint = glm::inverse(pointTransform);
        constexpr float epsilon = 1e-4f;
        const glm::vec3 localDomainMin = points->domainMinimum - glm::vec3(epsilon);
        const glm::vec3 localDomainMax = points->domainMaximum + glm::vec3(epsilon);

        for (std::size_t i = 0; i < package.globalRemeshHandles.size(); ++i) {
            std::array<float, 16> remeshToWorld{};
            if (!payloads.resolveLocalToWorld(package.globalRemeshHandles[i], remeshToWorld)) {
                errors.push_back("Voronoi Global domain cannot resolve a model placement.");
                return false;
            }
            package.globalRemeshLocalToWorld.push_back(remeshToWorld);
            const RemeshPackage* remesh = packages.findAny<RemeshPackage>(
                package.globalRemeshHandles[i].key);
            if (!remesh) {
                errors.push_back("Voronoi Global domain cannot resolve remesh package.");
                return false;
            }

            const glm::mat4 remeshToWorldMat = toMat4(remeshToWorld);
            const glm::mat4 remeshToPointLocal = worldToPoint * remeshToWorldMat;
            const std::vector<float>& positions = remesh->sourceGeometry.pointPositions;

            for (std::size_t p = 0; p + 2 < positions.size(); p += 3) {
                const glm::vec3 pointLocalPos = glm::vec3(
                    remeshToPointLocal * glm::vec4(positions[p], positions[p + 1], positions[p + 2], 1.0f));

                if (glm::any(glm::lessThan(pointLocalPos, localDomainMin)) ||
                    glm::any(glm::greaterThan(pointLocalPos, localDomainMax))) {
                    errors.push_back(
                        "Voronoi Global domain remesh " + std::to_string(package.globalRemeshHandles[i].key) +
                        " lies outside the PointData domain bounds.");
                    return false;
                }
            }
        }
    }
    HashPackage::seal(package, output.hashes);
    packages.apply<VoronoiPackage>(outputSocketKey, package);
    return true;
}

bool RuntimePackageCompiler::compileHeatPackage(
    const NodeGraphNode& node,
    uint64_t outputSocketKey,
    const NodeDataBlock& output,
    const NodePayloadRegistry& payloads,
    units::LengthUnit worldUnit,
    RuntimePackageManager& packages,
    std::vector<std::string>& errors) const {
    const HeatData* heat = payloads.get<HeatData>(output.payloadHandle);
    if (!heat) return true;

    HeatPackage package{};
    const HeatPackage* previous = packages.findStored<HeatPackage>(outputSocketKey);
    package.productHandle = previous ? previous->productHandle : ProductHandle{};
    package.authored = *heat;
    package.heatHandle = output.payloadHandle;
    package.domainVoronoiHandle = heat->domainVoronoiHandle;
    const HeatSolveNodeParams params = readHeatSolveNodeParams(node);
    package.display.showHeatOverlay = params.preview.showHeatOverlay;
    package.display.showFluxVectors = params.preview.showFluxVectors;
    package.display.showHeatPalette = params.preview.showHeatPalette;
    package.display.showContactLevelSet = params.preview.showContactLevelSet;
    package.display.fluxVectorScale = static_cast<float>(params.preview.fluxVectorScale);
    package.display.contactLevelSetRange = static_cast<float>(params.preview.contactLevelSetRange);

    std::unordered_map<uint64_t, const HeatModelData*> modelsByRemesh;
    for (const NodeDataHandle& handle : heat->heatModelHandles) {
        const HeatModelData* model = payloads.get<HeatModelData>(handle);
        if (model && model->meshHandle.key != 0) {
            modelsByRemesh.emplace(model->meshHandle.key, model);
        }
    }

    const bool canResolveDomain = heat->domainVoronoiHandle.key != 0 &&
        heat->activeGlobalVoronoiCount == 1 &&
        heat->activeVoronoiCount == heat->activeGlobalVoronoiCount;

    if (heat->active) {
        if (!canResolveDomain) {
            if (heat->activeVoronoiCount == 0) {
                errors.push_back("HeatSolve requires exactly one active DomainType::Global Voronoi input (none found).");
            } else {
                errors.push_back("HeatSolve requires a DomainType::Global Voronoi input; Mesh and Points domain products are rejected.");
            }
            return false;
        }

        const VoronoiData* voronoi = payloads.get<VoronoiData>(heat->domainVoronoiHandle);
        if (!voronoi || !voronoi->active || voronoi->domainType != DomainType::Global) {
            errors.push_back("HeatSolve's domain Voronoi input is invalid.");
            return false;
        }
    }

    const VoronoiPackage* domainPackage = canResolveDomain
        ? packages.findAny<VoronoiPackage>(heat->domainVoronoiHandle.key)
        : nullptr;
    if (canResolveDomain && (!domainPackage || !domainPackage->productHandle.isValid() ||
        domainPackage->domainType != DomainType::Global)) {
        if (heat->active) {
            errors.push_back("HeatSolve is missing its global Voronoi domain product.");
            return false;
        }
        domainPackage = nullptr;
    }

    if (heat->active) {
        if (modelsByRemesh.empty()) {
            errors.push_back("HeatSolve requires at least one heat model.");
            return false;
        }

        std::unordered_set<uint64_t> domainRemeshKeys;
        for (const NodeDataHandle& handle : domainPackage->globalRemeshHandles) {
            domainRemeshKeys.insert(handle.key);
        }
        if (domainRemeshKeys.size() != modelsByRemesh.size()) {
            errors.push_back("Heat package requires the heat-model remesh set to exactly equal the global Voronoi domain's Remeshes set.");
            return false;
        }
    }

    for (const auto& [remeshKey, model] : modelsByRemesh) {
        (void)remeshKey;
        const RemeshPackage* remeshPackage = packages.findAny<RemeshPackage>(model->meshHandle.key);
        const ModelPackage* modelPackage = remeshPackage
            ? packages.findAny<ModelPackage>(remeshPackage->sourceMeshHandle.key)
            : nullptr;
        if (!modelPackage || !remeshPackage ||
            !modelPackage->productHandle.isValid() ||
            !remeshPackage->productHandle.isValid()) {
            if (heat->active) {
                errors.push_back("Heat package is missing an upstream runtime product.");
                return false;
            }
            continue;
        }
        if (heat->active) {
            bool inDomain = false;
            for (const ProductHandle& handle : domainPackage->globalRemeshProducts) {
                inDomain = inDomain || handle == remeshPackage->productHandle;
            }
            if (!inDomain) {
                errors.push_back("Heat package contains a heat model outside the global Voronoi domain.");
                return false;
            }
        }

        HeatModelPackage compiledModel{};
        compiledModel.modelProduct = modelPackage->productHandle;
        compiledModel.remeshProduct = remeshPackage->productHandle;
        compiledModel.localToWorld = modelPackage->localToWorld;
        if (!payloads.resolveLocalToWorld(model->meshHandle, compiledModel.remeshLocalToWorld)) {
            if (heat->active) {
                errors.push_back("Heat package is missing the remesh placement for a heat model.");
                return false;
            }
            continue;
        }
        compiledModel.density = model->density;
        compiledModel.specificHeat = model->specificHeat;
        compiledModel.conductivity = model->conductivity;
        compiledModel.initialTemperatureC = model->initialTemperatureC;
        compiledModel.boundaryConditionType = static_cast<uint32_t>(
            model->boundaryCondition.type);
        compiledModel.boundaryTemperatureC = model->boundaryCondition.temperatureC;
        compiledModel.boundaryHeatFlux = model->boundaryCondition.heatFlux;
        compiledModel.boundaryHeatTransferCoefficient =
            model->boundaryCondition.heatTransferCoefficient;
        compiledModel.volumetricPowerDensity =
            model->volumetricHeatSource.powerDensity;

        if (model->boundaryCondition.type == BoundaryCondition::Type::RobinConvection &&
            model->robinTemperatureSourceHandle.key != 0) {
            const SerialTemperatureData* serial = payloads.get<SerialTemperatureData>(
                model->robinTemperatureSourceHandle);
            if (serial) {
                compiledModel.robinSourceKey = model->robinTemperatureSourceHandle.key;
                package.resolvedSerialSources[compiledModel.robinSourceKey] = *serial;
            }
        }

        package.models.push_back(compiledModel);
    }

    runtimepackage::CoordinatePass{}.run(package, worldUnit);
    if (domainPackage) {
        package.domainVoronoiProduct = domainPackage->productHandle;
    }
    HashPackage::seal(package, output.hashes);
    packages.apply<HeatPackage>(outputSocketKey, package);
    return true;
}

bool RuntimePackageCompiler::compilePointPackage(
    const NodeGraphState& graph,
    const NodeGraphNode& node,
    uint64_t outputSocketKey,
    const NodeDataBlock& output,
    const NodePayloadRegistry& payloads,
    const RuntimeProductManager& products,
    units::LengthUnit worldUnit,
    RuntimePackageManager& packages,
    std::vector<std::string>& errors) const {
    const PointData* points = payloads.resolvePoints(output.payloadHandle);
    if (!points || !points->active) return true;
    const NodeTypeId typeId = getNodeTypeId(node.typeId);
    const runtimepackage::CoordinatePass coordinatePass{};
    PointPackage package{};
    const PointPackage* previous = packages.findStored<PointPackage>(outputSocketKey);
    package.productHandle = previous ? previous->productHandle : ProductHandle{};
    package.pointsPayloadHandle = output.payloadHandle;
    package.domainMinimum = points->domainMinimum;
    package.domainMaximum = points->domainMaximum;

    if (typeId == nodegraphtypes::MeshPoints) {
        if (node.inputs.empty()) {
            errors.push_back("Mesh Points node has no remesh input.");
            return false;
        }
        const uint64_t upstreamKey = graph.edges.upstream(node.id, node.inputs.front().id).value;
        const RemeshPackage* remeshPackage = packages.findAny<RemeshPackage>(upstreamKey);
        const RemeshProduct* remeshProduct = remeshPackage
            ? products.resolve<RemeshProduct>(remeshPackage->productHandle)
            : nullptr;
        if (!remeshProduct || !remeshProduct->isValid()) {
            errors.push_back("Mesh Points package is missing its remesh product.");
            return false;
        }
        package.localToWorld = points->localToWorld;
        coordinatePass.run(package, worldUnit);
        package.positions.reserve(remeshProduct->surfacePositions.size());
        for (const glm::vec3& position : remeshProduct->surfacePositions) {
            package.positions.emplace_back(position, 1.0f);
        }
    } else if (typeId == nodegraphtypes::Transform) {
        if (!payloads.resolveLocalToWorld(output.payloadHandle, package.localToWorld)) {
            errors.push_back("Point Transform has no resolvable placement.");
            return false;
        }
        package.positions = points->positions;
        coordinatePass.run(package, worldUnit);
    } else if (typeId == nodegraphtypes::Merge) {
        const PointPackage* reference = nullptr;
        for (const NodeGraphSocket& input : node.inputs) {
            for (NodeSocketKey key : graph.edges.upstreams(node.id, input.id)) {
                const PointPackage* upstream = packages.findAny<PointPackage>(key.value);
                if (upstream && !upstream->positions.empty()) {
                    reference = upstream;
                    break;
                }
            }
            if (reference) break;
        }
        if (!reference) {
            errors.push_back("Point Merge package has no valid point input.");
            return false;
        }
        package.localToWorld = reference->localToWorld;
        package.domainMinimum = glm::vec3(std::numeric_limits<float>::max());
        package.domainMaximum = glm::vec3(std::numeric_limits<float>::lowest());
        const glm::mat4 inverseReference = glm::inverse(toMat4(package.localToWorld));
        for (const NodeGraphSocket& input : node.inputs) {
            for (NodeSocketKey key : graph.edges.upstreams(node.id, input.id)) {
                const PointPackage* upstream = packages.findAny<PointPackage>(key.value);
                if (!upstream || upstream->positions.empty()) continue;
                const glm::mat4 upstreamToReference =
                    inverseReference * toMat4(upstream->localToWorld);
                for (const glm::vec4& position : upstream->positions) {
                    package.positions.push_back(upstreamToReference * position);
                }
                for (uint32_t corner = 0; corner < 8; ++corner) {
                    const glm::vec4 point(
                        (corner & 1u) ? upstream->domainMaximum.x : upstream->domainMinimum.x,
                        (corner & 2u) ? upstream->domainMaximum.y : upstream->domainMinimum.y,
                        (corner & 4u) ? upstream->domainMaximum.z : upstream->domainMinimum.z,
                        1.0f);
                    const glm::vec3 transformed = glm::vec3(upstreamToReference * point);
                    package.domainMinimum = glm::min(package.domainMinimum, transformed);
                    package.domainMaximum = glm::max(package.domainMaximum, transformed);
                }
            }
        }
    } else {
        package.positions = points->positions;
        if (!payloads.resolveLocalToWorld(output.payloadHandle, package.localToWorld)) {
            errors.push_back("Point package has no resolvable placement.");
            return false;
        }
        coordinatePass.run(package, worldUnit);
    }

    if (package.positions.empty()) {
        errors.push_back("Point package contains no positions.");
        return false;
    }
    package.pointCount = static_cast<uint32_t>(package.positions.size());
    HashPackage::seal(package, output.hashes);
    packages.apply<PointPackage>(outputSocketKey, package);
    return true;
}
