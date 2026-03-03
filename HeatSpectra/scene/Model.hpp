#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <array>
#include <cstdint>

#include "util/File_utils.h"
#include "util/Structs.hpp"

class Camera;
class VulkanDevice;
class MemoryAllocator;

struct Vertex {
    glm::vec3 pos;      // Vertex position
    glm::vec3 color;    // Vertex color
    glm::vec3 normal;   // Vertex normal
    glm::vec2 texCoord; // Texture coordinates

    static std::array<VkVertexInputBindingDescription, 1> getBindingDescriptions() {
        std::array<VkVertexInputBindingDescription, 1> bindingDescriptions{};

        // Main vertex binding (positions, normals, texcoords, colors)
        VkVertexInputBindingDescription mainBinding{};
        mainBinding.binding         = 0;
        mainBinding.stride          = sizeof(Vertex); 
        mainBinding.inputRate       = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions[0]      = mainBinding;

        return bindingDescriptions;
    }

    // Separate attribute description functions
    static std::array<VkVertexInputAttributeDescription, 4> getVertexAttributes() {
        std::array<VkVertexInputAttributeDescription, 4> vertexAttributes{};

        vertexAttributes[0].binding     = 0;
        vertexAttributes[0].location    = 0;
        vertexAttributes[0].format      = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[0].offset      = offsetof(Vertex, pos);

        vertexAttributes[1].binding     = 0;
        vertexAttributes[1].location    = 1;
        vertexAttributes[1].format      = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[1].offset      = offsetof(Vertex, color);

        vertexAttributes[2].binding     = 0;
        vertexAttributes[2].location    = 2;
        vertexAttributes[2].format      = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[2].offset      = offsetof(Vertex, normal);

        vertexAttributes[3].binding     = 0;
        vertexAttributes[3].location    = 3;
        vertexAttributes[3].format      = VK_FORMAT_R32G32_SFLOAT;
        vertexAttributes[3].offset      = offsetof(Vertex, texCoord);

        return vertexAttributes;
    }

    bool operator==(const Vertex& other) const {
        return pos == other.pos &&
            color == other.color &&
            normal == other.normal &&
            texCoord == other.texCoord;
    }
};

struct ModelCornerKey {
    int32_t vertexIndex = -1;
    int32_t texcoordIndex = -1;
    int32_t normalIndex = -1;

    bool operator==(const ModelCornerKey& other) const {
        return vertexIndex == other.vertexIndex &&
            texcoordIndex == other.texcoordIndex &&
            normalIndex == other.normalIndex;
    }
};

struct ModelCornerKeyHash {
    size_t operator()(const ModelCornerKey& key) const {
        size_t h0 = std::hash<int32_t>{}(key.vertexIndex);
        size_t h1 = std::hash<int32_t>{}(key.texcoordIndex);
        size_t h2 = std::hash<int32_t>{}(key.normalIndex);
        return h0 ^ (h1 << 1) ^ (h2 << 2);
    }
};

namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            size_t h1 = std::hash<float>{}(vertex.pos.x) ^ std::hash<float>{}(vertex.pos.y) ^ std::hash<float>{}(vertex.pos.z);
            size_t h2 = std::hash<float>{}(vertex.normal.x) ^ std::hash<float>{}(vertex.normal.y) ^ std::hash<float>{}(vertex.normal.z);
            size_t h3 = std::hash<float>{}(vertex.texCoord.x) ^ std::hash<float>{}(vertex.texCoord.y);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
}

class CommandPool;

class Model {
public:
    Model(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, Camera& camera, CommandPool& commandPool);
    ~Model();
    bool init(const std::string modelPath);

    bool loadModel(const std::string& modelPath);

    void createVertexBuffer();
    void createIndexBuffer();
    void createRenderVertexBuffer();
    void createRenderIndexBuffer();

    void equalizeFaceAreas();
    void recalculateNormals();
    void updateGeometry(const std::vector<Vertex>& newVertices, const std::vector<uint32_t>& newIndices);
    void updateVertexBuffer();
    void updateIndexBuffer();
    void updateRenderVertexBuffer();
    void updateRenderIndexBuffer();
    void saveOBJ(const std::string& path) const;
    
    void translate(const glm::vec3& translation);
    void rotate(float angleRadians, const glm::vec3& axis, const glm::vec3& pivot);

    void recreateBuffers();
    void cleanup();

    glm::vec3 getBoundingBoxCenter();
    glm::vec3 getBoundingBoxMin();
    glm::vec3 getBoundingBoxMax();
    std::array<glm::vec3, 8> calculateBoundingBox(const std::vector<Vertex>& vertices, glm::vec3& mindBound, glm::vec3& maxBound);
    
    glm::vec3 getTranslationOffset() const { 
        return glm::vec3(modelMatrix[3]); 
    }

    const std::vector<Vertex>& getVertices() const {
        return vertices;
    }
    size_t getVertexCount() const {
        return vertices.size();
    }
    const std::vector<uint32_t>& getIndices() const {
        return indices;
    }

    const std::vector<Vertex>& getRenderVertices() const {
        return renderVertices;
    }
    const std::vector<uint32_t>& getRenderIndices() const {
        return renderIndices;
    }

    glm::vec3 getFaceNormal(uint32_t faceIndex) const;

    VkBuffer getVertexBuffer() const {
        return vertexBuffer;
    }
    VkDeviceSize getVertexBufferOffset() const {
        return vertexBufferOffset_;
    }

    VkBuffer getIndexBuffer() const {
        return indexBuffer;
    }
    VkDeviceSize getIndexBufferOffset() const {
        return indexBufferOffset_;
    }

    VkBuffer getRenderVertexBuffer() const {
        return renderVertexBuffer != VK_NULL_HANDLE ? renderVertexBuffer : vertexBuffer;
    }
    VkDeviceSize getRenderVertexBufferOffset() const {
        return renderVertexBuffer != VK_NULL_HANDLE ? renderVertexBufferOffset_ : vertexBufferOffset_;
    }

    VkBuffer getRenderIndexBuffer() const {
        return renderIndexBuffer != VK_NULL_HANDLE ? renderIndexBuffer : indexBuffer;
    }
    VkDeviceSize getRenderIndexBufferOffset() const {
        return renderIndexBuffer != VK_NULL_HANDLE ? renderIndexBufferOffset_ : indexBufferOffset_;
    }

    glm::vec3 getModelPosition() {
        return modelPosition;
    }

    glm::mat4 getModelMatrix() {
        return modelMatrix;
    }

    uint32_t getRuntimeModelId() const {
        return runtimeModelId;
    }


    void setModelPosition(const glm::vec3& position) { 
        modelPosition = position; 
    }
    void setModelMatrix(const glm::mat4& matrix) {
        modelMatrix = matrix;
    }
    void setIndices(const std::vector<uint32_t>& newIndices) {
        indices = newIndices;
        renderIndices = indices;
        hasSplitRenderMesh = false;
    }
    void setVertices(const std::vector<Vertex>& newVertices) { 
        vertices = newVertices;
        renderVertices = vertices;
        hasSplitRenderMesh = false;
    }
    void setRuntimeModelId(uint32_t id) {
        runtimeModelId = id;
    }

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Camera& camera;
    CommandPool& commandPool;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Vertex> renderVertices;
    std::vector<uint32_t> renderIndices;

    bool hasSplitRenderMesh = false;

	VkBuffer vertexBuffer;
    VkDeviceSize vertexBufferOffset_;

	VkBuffer indexBuffer;
    VkDeviceSize indexBufferOffset_;

    VkBuffer renderVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize renderVertexBufferOffset_ = 0;

    VkBuffer renderIndexBuffer = VK_NULL_HANDLE;
    VkDeviceSize renderIndexBufferOffset_ = 0;

    glm::vec3 modelPosition = glm::vec3(0.0f);
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    uint32_t runtimeModelId = 0;
}; 
