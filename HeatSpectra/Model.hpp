#pragma once

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include "File_utils.h" 
#include "Structs.hpp"

#include <unordered_map>
#include <string>
#include <vector>
#include <array>

class VulkanDevice;

const std::string MODEL_PATH = "C:/Users/tsundoku/Documents/Visual Studio 2022/Projects/HeatSpectra/HeatSpectra/models/teapot.obj"; //change
const std::string TEXTURE_PATH = "C:/Users/tsundoku/Documents/Visual Studio 2022/Projects/HeatSpectra/HeatSpectra/textures/texture.jpg"; //change

struct Vertex {
    glm::vec3 pos;      // Vertex position
    glm::vec3 color;    // Vertex color
    glm::vec3 normal;   // Vertex normal
    glm::vec2 texCoord; // Texture coordinates

    static std::array<VkVertexInputBindingDescription, 2> getBindingDescriptions() {
        std::array<VkVertexInputBindingDescription, 2> bindingDescriptions{};

        // Main vertex binding (positions, normals, texcoords)
        VkVertexInputBindingDescription mainBinding{};
        mainBinding.binding = 0;
        mainBinding.stride = sizeof(Vertex); 
        mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions[0] = mainBinding;

        // Surface binding (dynamic color from compute shader)
        VkVertexInputBindingDescription surfaceBinding{};
        surfaceBinding.binding = 1;
        surfaceBinding.stride = sizeof(SurfaceVertex); 
        surfaceBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions[1] = surfaceBinding;

        return bindingDescriptions;
    }

    // Separate attribute description functions
    static std::array<VkVertexInputAttributeDescription, 4> getVertexAttributes() {
        std::array<VkVertexInputAttributeDescription, 4> vertexAttributes{};

        vertexAttributes[0].binding = 0;
        vertexAttributes[0].location = 0;
        vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[0].offset = offsetof(Vertex, pos);

        vertexAttributes[1].binding = 0;
        vertexAttributes[1].location = 1;
        vertexAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[1].offset = offsetof(Vertex, color);

        vertexAttributes[2].binding = 0;
        vertexAttributes[2].location = 2;
        vertexAttributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        vertexAttributes[2].offset = offsetof(Vertex, normal);

        vertexAttributes[3].binding = 0;
        vertexAttributes[3].location = 3;
        vertexAttributes[3].format = VK_FORMAT_R32G32_SFLOAT;
        vertexAttributes[3].offset = offsetof(Vertex, texCoord);

        return vertexAttributes;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getSurfaceVertexAttributes() {
        std::array<VkVertexInputAttributeDescription, 2> surfaceVertexAttributes{};

        surfaceVertexAttributes[0].binding = 1;
        surfaceVertexAttributes[0].location = 4;
        surfaceVertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        surfaceVertexAttributes[0].offset = offsetof(SurfaceVertex, position); 

        surfaceVertexAttributes[1].binding = 1;
        surfaceVertexAttributes[1].location = 5;
        surfaceVertexAttributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        surfaceVertexAttributes[1].offset = offsetof(SurfaceVertex, color);

        return surfaceVertexAttributes;
    }

    bool operator==(const Vertex& other) const {
        return pos == other.pos &&
            color == other.color &&
            normal == other.normal &&
            texCoord == other.texCoord;
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

class Model {
public:
    Model() = default;
    void init(VulkanDevice& vulkanDevice);

    void loadModel();
    void createVertexBuffer();
    void createIndexBuffer();

    void cleanup();

    glm::vec3 getBoundingBoxCenter();
    glm::vec3 calculateBoundingBox(const std::vector<Vertex>& vertices, glm::vec3& mindBound, glm::vec3& maxBound);

    HitResult rayIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir);

    std::vector<Vertex> getVertices() {
        return vertices;
    }
    size_t getVertexCount() {
        return vertices.size();
    }
    std::vector<uint32_t> getIndices() {
        return indices;
    }
    VkBuffer getVertexBuffer() {
        return vertexBuffer;
    }
    VkDeviceMemory getVertexBufferMemory() {
        return vertexBufferMemory;
    }
    VkBuffer getIndexBuffer() {
        return indexBuffer;
    }
    VkDeviceMemory getIndexBufferMemory() {
        return indexBufferMemory;
    }

private:
    VulkanDevice* vulkanDevice;

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	VkBuffer vertexBuffer{};
	VkDeviceMemory vertexBufferMemory{};
	VkBuffer indexBuffer{};
	VkDeviceMemory indexBufferMemory{};

}; 