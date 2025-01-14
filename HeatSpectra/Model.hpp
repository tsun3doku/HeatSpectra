#pragma once

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "File_utils.h" 

#include <unordered_map>
#include <string>
#include <vector>
#include <array>

class VulkanDevice;

const std::string MODEL_PATH = "C:/Users/tsundoku/Documents/Visual Studio 2022/Projects/HeatSpectra/HeatSpectra/models/bb.obj"; //change
const std::string TEXTURE_PATH = "C:/Users/tsundoku/Documents/Visual Studio 2022/Projects/HeatSpectra/HeatSpectra/textures/texture.jpg"; //change

struct Vertex {
    glm::vec3 pos;      // Vertex position
    glm::vec3 color;    // Vertex color
    glm::vec3 normal;   // Vertex normal
    glm::vec2 texCoord; // Texture coordinates

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};

        // Position attribute
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Color attribute
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        // Normal attribute 
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, normal);

        // Texture coordinate attribute
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
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
            return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1)
                ^ ((hash<glm::vec3>()(vertex.normal) ^ (hash<glm::vec2>()(vertex.texCoord) << 1)) >> 1);
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

    const std::vector<Vertex> getVertices() const {
        return vertices;
    }

    const std::vector<uint32_t> getIndices() const {
        return indices;
    }

    const VkBuffer getVertexBuffer() const {
        return vertexBuffer;
    }

    const VkDeviceMemory getVertexBufferMemory() const {
        return vertexBufferMemory;
    }

    const VkBuffer getIndexBuffer() const {
        return indexBuffer;
    }

    const VkDeviceMemory getIndexBufferMemory() const {
        return indexBufferMemory;
    }

private:
    glm::vec3 calculateBoundingBox(const std::vector<Vertex>& vertices, glm::vec3& mindBound, glm::vec3& maxBound);
	
    VulkanDevice* vulkanDevice;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	VkBuffer vertexBuffer{};
	VkDeviceMemory vertexBufferMemory{};
	VkBuffer indexBuffer{};
	VkDeviceMemory indexBufferMemory{};

}; 