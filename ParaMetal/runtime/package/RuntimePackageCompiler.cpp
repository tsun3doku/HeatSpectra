#include "RuntimePackageCompiler.hpp"

#include "domain/HeatModelData.hpp"
#include "domain/PointData.hpp"
#include "hash/HashPackage.hpp"
#include "nodegraph/NodeContactParams.hpp"
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
        else if (typeId == nodegraphtypes::Contact)
            compiled = compileContactPackage(node, outputSocketKey, output, payloads, worldUnit, packages, errors);
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
    package.modelRemeshHandle = voronoi->modelMeshHandle;
    package.pointsPayloadHandle = voronoi->pointsPayloadHandle;
    const VoronoiNodeParams params = readVoronoiNodeParams(node);
    package.display.showVoronoi = params.preview.showVoronoi;
    package.display.showPoints = params.preview.showPoints;

    if (package.domainType == DomainType::Mesh) {
        const GeometryData* geometry = payloads.resolveGeometry(voronoi->modelMeshHandle);
        const RemeshPackage* remesh = packages.findAny<RemeshPackage>(voronoi->modelMeshHandle.key);
        const ModelPackage* model = remesh
            ? packages.findAny<ModelPackage>(remesh->sourceMeshHandle.key)
            : nullptr;
        if (!geometry || !model || !remesh ||
            !model->productHandle.isValid() || !remesh->productHandle.isValid()) {
            errors.push_back("Voronoi package is missing its upstream mesh products.");
            return false;
        }
        package.modelMeshHandle = remesh->sourceMeshHandle;
        if (!payloads.resolveLocalToWorld(voronoi->modelMeshHandle, package.localToWorld)) {
            errors.push_back("Voronoi package cannot resolve model placement.");
            return false;
        }
        package.modelProduct = model->productHandle;
        package.remeshProduct = remesh->productHandle;
    }

    runtimepackage::CoordinatePass{}.run(package, worldUnit);
    package.pointPositions = points->positions;
    for (uint32_t corner = 0; corner < 8; ++corner) {
        package.pointDomainCorners[corner] = glm::vec3(
            (corner & 1u) ? points->domainMaximum.x : points->domainMinimum.x,
            (corner & 2u) ? points->domainMaximum.y : points->domainMinimum.y,
            (corner & 4u) ? points->domainMaximum.z : points->domainMinimum.z);
    }
    if (package.domainType == DomainType::Mesh) {
        const glm::mat4 pointsToMesh =
            glm::inverse(toMat4(package.localToWorld)) * toMat4(points->localToWorld);
        for (glm::vec4& position : package.pointPositions) position = pointsToMesh * position;
        for (glm::vec3& corner : package.pointDomainCorners)
            corner = glm::vec3(pointsToMesh * glm::vec4(corner, 1.0f));
    }
    HashPackage::seal(package, output.hashes);
    packages.apply<VoronoiPackage>(outputSocketKey, package);
    return true;
}

bool RuntimePackageCompiler::compileContactPackage(
    const NodeGraphNode& node,
    uint64_t outputSocketKey,
    const NodeDataBlock& output,
    const NodePayloadRegistry& payloads,
    units::LengthUnit worldUnit,
    RuntimePackageManager& packages,
    std::vector<std::string>& errors) const {
    const ContactData* contact = payloads.get<ContactData>(output.payloadHandle);
    if (!contact || !contact->active || !contact->pair.hasValidContact) return true;
    const GeometryData* geometryA = payloads.resolveGeometry(contact->pair.endpointA.meshHandle);
    const GeometryData* geometryB = payloads.resolveGeometry(contact->pair.endpointB.meshHandle);
    const RemeshPackage* remeshA = packages.findAny<RemeshPackage>(
        contact->pair.endpointA.meshHandle.key);
    const RemeshPackage* remeshB = packages.findAny<RemeshPackage>(
        contact->pair.endpointB.meshHandle.key);
    if (!geometryA || !geometryB || !remeshA || !remeshB ||
        !remeshA->productHandle.isValid() || !remeshB->productHandle.isValid()) {
        errors.push_back("Contact package is missing an upstream remesh product.");
        return false;
    }

    ContactPackage package{};
    const ContactPackage* previous = packages.findStored<ContactPackage>(outputSocketKey);
    package.productHandle = previous ? previous->productHandle : ProductHandle{};
    package.authored = *contact;
    package.contactHandle = output.payloadHandle;
    package.display.showContactLines = readContactNodeParams(node).preview.showContactLines;
    if (!payloads.resolveLocalToWorld(
            contact->pair.endpointA.meshHandle, package.modelALocalToWorld) ||
        !payloads.resolveLocalToWorld(
            contact->pair.endpointB.meshHandle, package.modelBLocalToWorld)) {
        errors.push_back("Contact package cannot resolve model placement.");
        return false;
    }
    package.modelARemeshProduct = remeshA->productHandle;
    package.modelBRemeshProduct = remeshB->productHandle;
    runtimepackage::CoordinatePass{}.run(package, worldUnit);
    HashPackage::seal(package, output.hashes);
    packages.apply<ContactPackage>(outputSocketKey, package);
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
    const HeatSolveNodeParams params = readHeatSolveNodeParams(node);
    package.display.showHeatOverlay = params.preview.showHeatOverlay;
    package.display.showFluxVectors = params.preview.showFluxVectors;
    package.display.showHeatPalette = params.preview.showHeatPalette;
    package.display.fluxVectorScale = static_cast<float>(params.preview.fluxVectorScale);

    std::unordered_map<uint64_t, const HeatModelData*> modelsByRemesh;
    for (const NodeDataHandle& handle : heat->heatModelHandles) {
        const HeatModelData* model = payloads.get<HeatModelData>(handle);
        if (!model || model->meshHandle.key == 0 ||
            !modelsByRemesh.emplace(model->meshHandle.key, model).second) {
            errors.push_back("Heat package contains an invalid heat model.");
            return false;
        }
    }

    std::unordered_set<uint64_t> usedModels;
    for (const NodeDataHandle& voronoiHandle : heat->voronoiHandles) {
        const VoronoiData* voronoi = payloads.get<VoronoiData>(voronoiHandle);
        if (!voronoi || !voronoi->active) {
            errors.push_back("Heat package contains an invalid Voronoi domain.");
            return false;
        }
        const auto modelIt = modelsByRemesh.find(voronoi->modelMeshHandle.key);
        if (modelIt == modelsByRemesh.end()) {
            errors.push_back("Heat package cannot match a Voronoi domain to a heat model.");
            return false;
        }

        const HeatModelData* model = modelIt->second;
        const RemeshPackage* remeshPackage = packages.findAny<RemeshPackage>(model->meshHandle.key);
        const VoronoiPackage* voronoiPackage = packages.findAny<VoronoiPackage>(voronoiHandle.key);
        const ModelPackage* modelPackage = remeshPackage
            ? packages.findAny<ModelPackage>(remeshPackage->sourceMeshHandle.key)
            : nullptr;
        if (!modelPackage || !remeshPackage || !voronoiPackage ||
            !modelPackage->productHandle.isValid() ||
            !remeshPackage->productHandle.isValid() ||
            !voronoiPackage->productHandle.isValid() ||
            !(voronoiPackage->remeshProduct == remeshPackage->productHandle)) {
            errors.push_back("Heat package is missing an upstream runtime product.");
            return false;
        }

        HeatModelPackage compiledModel{};
        compiledModel.modelProduct = modelPackage->productHandle;
        compiledModel.remeshProduct = remeshPackage->productHandle;
        compiledModel.voronoiProduct = voronoiPackage->productHandle;
        compiledModel.localToWorld = modelPackage->localToWorld;
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

        usedModels.insert(modelIt->first);
        package.models.push_back(compiledModel);
    }
    if (usedModels.size() != modelsByRemesh.size()) {
        errors.push_back("Heat package has an unused heat model.");
        return false;
    }

    for (const NodeDataHandle& contactHandle : heat->contactHandles) {
        const ContactPackage* contactPackage = packages.findAny<ContactPackage>(contactHandle.key);
        bool hasA = false;
        bool hasB = false;
        if (contactPackage) {
            for (const HeatModelPackage& model : package.models) {
                hasA = hasA || model.remeshProduct == contactPackage->modelARemeshProduct;
                hasB = hasB || model.remeshProduct == contactPackage->modelBRemeshProduct;
            }
        }
        if (!contactPackage || !contactPackage->productHandle.isValid() || !hasA || !hasB) {
            errors.push_back("Heat package contains an invalid contact dependency.");
            return false;
        }
        package.contactProducts.push_back(contactPackage->productHandle);
    }

    runtimepackage::CoordinatePass{}.run(package, worldUnit);
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
