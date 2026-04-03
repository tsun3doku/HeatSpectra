#pragma once

#include "heat/VoronoiSystemController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimePackages.hpp"

#include <unordered_map>

class RuntimeVoronoiTransport {
public:
    void setController(VoronoiSystemController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void apply(uint64_t socketKey, const VoronoiPackage* package) {
        if (!controller) {
            return;
        }

        if (!package ||
            !package->authored.active ||
            package->receiverRemeshProducts.empty()) {
            controller->disable();
            removePublishedProduct();
            return;
        }

        VoronoiSystemController::Config config{};
        config.active = true;
        config.params = package->authored.params;
        const size_t receiverCount = package->receiverRemeshProducts.size();
        config.receiverRuntimeModelIds.resize(receiverCount, 0);
        config.receiverNodeModelIds.resize(receiverCount, 0);
        config.receiverGeometryPositions.resize(receiverCount);
        config.receiverGeometryTriangleIndices.resize(receiverCount);
        config.receiverIntrinsicMeshes.resize(receiverCount);
        config.receiverSurfaceVertices.resize(receiverCount);
        config.receiverIntrinsicTriangleIndices.resize(receiverCount);
        config.meshVertexBuffers.resize(receiverCount, VK_NULL_HANDLE);
        config.meshVertexBufferOffsets.resize(receiverCount, 0);
        config.meshIndexBuffers.resize(receiverCount, VK_NULL_HANDLE);
        config.meshIndexBufferOffsets.resize(receiverCount, 0);
        config.meshIndexCounts.resize(receiverCount, 0);
        config.meshModelMatrices.resize(receiverCount, glm::mat4(1.0f));
        config.supportingHalfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
        config.supportingAngleViews.resize(receiverCount, VK_NULL_HANDLE);
        config.halfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
        config.edgeViews.resize(receiverCount, VK_NULL_HANDLE);
        config.triangleViews.resize(receiverCount, VK_NULL_HANDLE);
        config.lengthViews.resize(receiverCount, VK_NULL_HANDLE);
        config.inputHalfedgeViews.resize(receiverCount, VK_NULL_HANDLE);
        config.inputEdgeViews.resize(receiverCount, VK_NULL_HANDLE);
        config.inputTriangleViews.resize(receiverCount, VK_NULL_HANDLE);
        config.inputLengthViews.resize(receiverCount, VK_NULL_HANDLE);

        for (size_t index = 0; index < package->receiverRemeshProducts.size() && index < receiverCount; ++index) {
            const ProductHandle& remeshProductHandle = package->receiverRemeshProducts[index];
            const RemeshProduct* product =
                productRegistry ? productRegistry->resolveRemesh(remeshProductHandle) : nullptr;
            if (!product || !product->isValid()) {
                continue;
            }

            config.receiverRuntimeModelIds[index] = product->runtimeModelId;
            config.receiverNodeModelIds[index] = product->geometry.modelId;
            config.receiverGeometryPositions[index] = product->geometryPositions;
            config.receiverGeometryTriangleIndices[index] = product->geometryTriangleIndices;
            config.receiverIntrinsicMeshes[index] = product->intrinsicMesh;
            config.receiverIntrinsicTriangleIndices[index] = product->intrinsicMesh.indices;
            config.receiverSurfaceVertices[index].reserve(product->intrinsicMesh.vertices.size());
            for (const SupportingHalfedge::IntrinsicVertex& intrinsicVertex : product->intrinsicMesh.vertices) {
                VoronoiGeometryRuntime::SurfaceVertex vertex{};
                vertex.position = intrinsicVertex.position;
                vertex.normal = intrinsicVertex.normal;
                config.receiverSurfaceVertices[index].push_back(vertex);
            }
            config.supportingHalfedgeViews[index] = product->supportingHalfedgeView;
            config.supportingAngleViews[index] = product->supportingAngleView;
            config.halfedgeViews[index] = product->halfedgeView;
            config.edgeViews[index] = product->edgeView;
            config.triangleViews[index] = product->triangleView;
            config.lengthViews[index] = product->lengthView;
            config.inputHalfedgeViews[index] = product->inputHalfedgeView;
            config.inputEdgeViews[index] = product->inputEdgeView;
            config.inputTriangleViews[index] = product->inputTriangleView;
            config.inputLengthViews[index] = product->inputLengthView;
        }

        for (size_t index = 0; index < package->receiverModelProducts.size(); ++index) {
            if (index >= receiverCount) {
                break;
            }

            const ProductHandle& modelProductHandle = package->receiverModelProducts[index];
            const ModelProduct* modelProduct =
                productRegistry ? productRegistry->resolveModel(modelProductHandle) : nullptr;
            if (!modelProduct || !modelProduct->isValid()) {
                continue;
            }

            config.meshVertexBuffers[index] = modelProduct->vertexBuffer;
            config.meshVertexBufferOffsets[index] = modelProduct->vertexBufferOffset;
            config.meshIndexBuffers[index] = modelProduct->indexBuffer;
            config.meshIndexBufferOffsets[index] = modelProduct->indexBufferOffset;
            config.meshIndexCounts[index] = modelProduct->indexCount;
            config.meshModelMatrices[index] = modelProduct->modelMatrix;
        }
        controller->configure(config);

        publishProduct(socketKey);
    }

private:
    void removePublishedProduct() {
        if (!productRegistry || publishedSocketKey == 0) {
            publishedSocketKey = 0;
            return;
        }

        productRegistry->removeVoronoi(publishedSocketKey);
        publishedSocketKey = 0;
    }

    void publishProduct(uint64_t socketKey) {
        if (publishedSocketKey != 0 && publishedSocketKey != socketKey) {
            removePublishedProduct();
        }

        if (!productRegistry || !controller || socketKey == 0) {
            return;
        }

        VoronoiProduct product{};
        if (!controller->exportProduct(product)) {
            productRegistry->removeVoronoi(socketKey);
            if (publishedSocketKey == socketKey) {
                publishedSocketKey = 0;
            }
            return;
        }

        productRegistry->publishVoronoi(socketKey, product);
        publishedSocketKey = socketKey;
    }

    VoronoiSystemController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    uint64_t publishedSocketKey = 0;
};
