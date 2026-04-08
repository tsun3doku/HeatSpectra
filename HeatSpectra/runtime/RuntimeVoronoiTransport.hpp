#pragma once

#include "heat/VoronoiSystemController.hpp"
#include "runtime/RuntimeProductRegistry.hpp"
#include "runtime/RuntimePackages.hpp"

#include <unordered_map>
#include <unordered_set>
class RuntimeVoronoiTransport {
public:
    void setController(VoronoiSystemController* updatedController) {
        controller = updatedController;
    }

    void setProductRegistry(RuntimeProductRegistry* updatedRegistry) {
        productRegistry = updatedRegistry;
    }

    void sync(const std::unordered_map<uint64_t, VoronoiPackage>& packagesBySocket) {
        if (!controller) {
            return;
        }

        std::unordered_set<uint64_t> activeSockets;

        for (const auto& [socketKey, package] : packagesBySocket) {
            if (!package.authored.active || package.receiverRemeshProducts.empty()) {
                continue;
            }

            VoronoiSystemController::Config config{};
            bool isPackageReady = true;
            config.active = true;
            config.params = package.authored.params;
            const size_t receiverCount = package.receiverRemeshProducts.size();
            if (package.receiverGeometries.size() < receiverCount ||
                package.receiverModelProducts.size() < receiverCount) {
                isPackageReady = false;
            }
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

            for (size_t index = 0; isPackageReady && index < package.receiverRemeshProducts.size() && index < receiverCount; ++index) {
                const ProductHandle& remeshProductHandle = package.receiverRemeshProducts[index];
                const RemeshProduct* product =
                    productRegistry ? productRegistry->resolveRemesh(remeshProductHandle) : nullptr;
                if (!product) {
                    isPackageReady = false;
                    break;
                }

                config.receiverRuntimeModelIds[index] = product->runtimeModelId;
                config.receiverNodeModelIds[index] = package.receiverGeometries[index].modelId;
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

            for (size_t index = 0; isPackageReady && index < receiverCount; ++index) {
                const ProductHandle& modelProductHandle = package.receiverModelProducts[index];
                const ModelProduct* modelProduct =
                    productRegistry ? productRegistry->resolveModel(modelProductHandle) : nullptr;
                if (!modelProduct) {
                    isPackageReady = false;
                    break;
                }

                config.meshVertexBuffers[index] = modelProduct->vertexBuffer;
                config.meshVertexBufferOffsets[index] = modelProduct->vertexBufferOffset;
                config.meshIndexBuffers[index] = modelProduct->indexBuffer;
                config.meshIndexBufferOffsets[index] = modelProduct->indexBufferOffset;
                config.meshIndexCounts[index] = modelProduct->indexCount;
                config.meshModelMatrices[index] = modelProduct->modelMatrix;
            }

            if (!isPackageReady) {
                const bool wasApplied = publishedSocketKeys.find(socketKey) != publishedSocketKeys.end();
                if (wasApplied) {
                    activeSockets.insert(socketKey);
                }
                continue;
            }

            controller->configure(socketKey, config);
            activeSockets.insert(socketKey);
            publishProduct(socketKey);
        }

        auto it = publishedSocketKeys.begin();
        while (it != publishedSocketKeys.end()) {
            if (activeSockets.find(*it) == activeSockets.end()) {
                controller->disable(*it);
                removePublishedProduct(*it);
                it = publishedSocketKeys.erase(it);
            } else {
                ++it;
            }
        }

        publishedSocketKeys = activeSockets;
    }

private:
    void removePublishedProduct(uint64_t socketKey) {
        if (productRegistry && socketKey != 0) {
            productRegistry->removeVoronoi(socketKey);
        }
    }

    void publishProduct(uint64_t socketKey) {
        if (!productRegistry || !controller || socketKey == 0) {
            return;
        }

        VoronoiProduct product{};
        if (!controller->exportProduct(socketKey, product)) {
            productRegistry->removeVoronoi(socketKey);
            return;
        }

        productRegistry->publishVoronoi(socketKey, product);
    }

    VoronoiSystemController* controller = nullptr;
    RuntimeProductRegistry* productRegistry = nullptr;
    std::unordered_set<uint64_t> publishedSocketKeys;
};
