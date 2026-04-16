#pragma once

#include "runtime/RuntimePackages.hpp"
#include "runtime/RuntimeProducts.hpp"

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <unordered_map>
#include <unordered_set>

class IntrinsicRenderer;

class RemeshDisplayController {
public:
    struct Config {
        bool showRemeshOverlay = false;
        bool showFaceNormals = false;
        bool showVertexNormals = false;
        float normalLength = 0.05f;
        uint32_t runtimeModelId = 0;
        VkBuffer renderVertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize renderVertexBufferOffset = 0;
        VkBuffer renderIndexBuffer = VK_NULL_HANDLE;
        VkDeviceSize renderIndexBufferOffset = 0;
        uint32_t renderIndexCount = 0;
        glm::mat4 modelMatrix{ 1.0f };
        VkBuffer intrinsicTriangleBuffer = VK_NULL_HANDLE;
        VkDeviceSize intrinsicTriangleBufferOffset = 0;
        VkBuffer intrinsicVertexBuffer = VK_NULL_HANDLE;
        VkDeviceSize intrinsicVertexBufferOffset = 0;
        size_t intrinsicTriangleCount = 0;
        size_t intrinsicVertexCount = 0;
        float averageTriangleArea = 0.0f;
        VkBufferView supportingHalfedgeView = VK_NULL_HANDLE;
        VkBufferView supportingAngleView = VK_NULL_HANDLE;
        VkBufferView halfedgeView = VK_NULL_HANDLE;
        VkBufferView edgeView = VK_NULL_HANDLE;
        VkBufferView triangleView = VK_NULL_HANDLE;
        VkBufferView lengthView = VK_NULL_HANDLE;
        VkBufferView inputHalfedgeView = VK_NULL_HANDLE;
        VkBufferView inputEdgeView = VK_NULL_HANDLE;
        VkBufferView inputTriangleView = VK_NULL_HANDLE;
        VkBufferView inputLengthView = VK_NULL_HANDLE;
        uint64_t contentHash = 0;

        bool anyVisible() const {
            return showRemeshOverlay || showFaceNormals || showVertexNormals;
        }

        bool isValid() const {
            return runtimeModelId != 0 &&
                renderVertexBuffer != VK_NULL_HANDLE &&
                renderIndexBuffer != VK_NULL_HANDLE &&
                renderIndexCount != 0 &&
                intrinsicTriangleBuffer != VK_NULL_HANDLE &&
                intrinsicVertexBuffer != VK_NULL_HANDLE &&
                supportingHalfedgeView != VK_NULL_HANDLE &&
                supportingAngleView != VK_NULL_HANDLE &&
                halfedgeView != VK_NULL_HANDLE &&
                edgeView != VK_NULL_HANDLE &&
                triangleView != VK_NULL_HANDLE &&
                lengthView != VK_NULL_HANDLE &&
                inputHalfedgeView != VK_NULL_HANDLE &&
                inputEdgeView != VK_NULL_HANDLE &&
                inputTriangleView != VK_NULL_HANDLE &&
                inputLengthView != VK_NULL_HANDLE;
        }

        bool operator==(const Config& other) const {
            return showRemeshOverlay == other.showRemeshOverlay &&
                showFaceNormals == other.showFaceNormals &&
                showVertexNormals == other.showVertexNormals &&
                normalLength == other.normalLength &&
                runtimeModelId == other.runtimeModelId &&
                renderVertexBuffer == other.renderVertexBuffer &&
                renderVertexBufferOffset == other.renderVertexBufferOffset &&
                renderIndexBuffer == other.renderIndexBuffer &&
                renderIndexBufferOffset == other.renderIndexBufferOffset &&
                renderIndexCount == other.renderIndexCount &&
                modelMatrix == other.modelMatrix &&
                intrinsicTriangleBuffer == other.intrinsicTriangleBuffer &&
                intrinsicTriangleBufferOffset == other.intrinsicTriangleBufferOffset &&
                intrinsicVertexBuffer == other.intrinsicVertexBuffer &&
                intrinsicVertexBufferOffset == other.intrinsicVertexBufferOffset &&
                intrinsicTriangleCount == other.intrinsicTriangleCount &&
                intrinsicVertexCount == other.intrinsicVertexCount &&
                averageTriangleArea == other.averageTriangleArea &&
                supportingHalfedgeView == other.supportingHalfedgeView &&
                supportingAngleView == other.supportingAngleView &&
                halfedgeView == other.halfedgeView &&
                edgeView == other.edgeView &&
                triangleView == other.triangleView &&
                lengthView == other.lengthView &&
                inputHalfedgeView == other.inputHalfedgeView &&
                inputEdgeView == other.inputEdgeView &&
                inputTriangleView == other.inputTriangleView &&
                inputLengthView == other.inputLengthView &&
                contentHash == other.contentHash;
        }
    };

    void setIntrinsicRenderer(IntrinsicRenderer* updatedIntrinsicRenderer);
    void apply(uint64_t socketKey, const Config& config);
    void remove(uint64_t socketKey);
    void finalizeSync();

private:
    IntrinsicRenderer* intrinsicRenderer = nullptr;
    std::unordered_map<uint64_t, Config> activeConfigsBySocket;
    std::unordered_set<uint64_t> touchedSocketKeys;
};

inline uint64_t computeContentHash(const RemeshDisplayController::Config& config) {
    uint64_t hash = 1469598103934665603ull;
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showRemeshOverlay ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showFaceNormals ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, static_cast<uint64_t>(config.showVertexNormals ? 1u : 0u));
    hash = RuntimeProductHash::mixPod(hash, config.normalLength);
    hash = RuntimeProductHash::mixPod(hash, config.runtimeModelId);
    hash = RuntimeProductHash::mixPod(hash, config.renderVertexBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.renderVertexBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.renderIndexBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.renderIndexBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.renderIndexCount);
    hash = RuntimeProductHash::mixPod(hash, config.modelMatrix);
    hash = RuntimeProductHash::mixPod(hash, config.intrinsicTriangleBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.intrinsicTriangleBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.intrinsicVertexBuffer);
    hash = RuntimeProductHash::mixPod(hash, config.intrinsicVertexBufferOffset);
    hash = RuntimeProductHash::mixPod(hash, config.intrinsicTriangleCount);
    hash = RuntimeProductHash::mixPod(hash, config.intrinsicVertexCount);
    hash = RuntimeProductHash::mixPod(hash, config.averageTriangleArea);
    hash = RuntimeProductHash::mixPod(hash, config.supportingHalfedgeView);
    hash = RuntimeProductHash::mixPod(hash, config.supportingAngleView);
    hash = RuntimeProductHash::mixPod(hash, config.halfedgeView);
    hash = RuntimeProductHash::mixPod(hash, config.edgeView);
    hash = RuntimeProductHash::mixPod(hash, config.triangleView);
    hash = RuntimeProductHash::mixPod(hash, config.lengthView);
    hash = RuntimeProductHash::mixPod(hash, config.inputHalfedgeView);
    hash = RuntimeProductHash::mixPod(hash, config.inputEdgeView);
    hash = RuntimeProductHash::mixPod(hash, config.inputTriangleView);
    hash = RuntimeProductHash::mixPod(hash, config.inputLengthView);
    return hash;
}
