#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanDevice.hpp"
#include "CommandBufferManager.hpp"
#include "Model.hpp"

void Model::init(VulkanDevice& vulkanDevice) {
    this->vulkanDevice = &vulkanDevice; // Reference to VulkanDevice class

    loadModel();
    createVertexBuffer();
    createIndexBuffer();
}

void Model::cleanup() {
    vkDestroyBuffer(vulkanDevice->getDevice(), indexBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), indexBufferMemory, nullptr);

    vkDestroyBuffer(vulkanDevice->getDevice(), vertexBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), vertexBufferMemory, nullptr);
}

glm::vec3 Model::calculateBoundingBox(const std::vector<Vertex>& vertices, glm::vec3& minBound, glm::vec3& maxBound) {
    // Initialize min and max bounding box values
    minBound = glm::vec3(FLT_MAX);  
    maxBound = glm::vec3(-FLT_MAX);

    // Iterate through all vertices to find the min and max coordinates
    for (const auto& vertex : vertices) {
        minBound.x = std::min(minBound.x, vertex.pos.x);
        minBound.y = std::min(minBound.y, vertex.pos.y);
        minBound.z = std::min(minBound.z, vertex.pos.z);

        maxBound.x = std::max(maxBound.x, vertex.pos.x);
        maxBound.y = std::max(maxBound.y, vertex.pos.y);
        maxBound.z = std::max(maxBound.z, vertex.pos.z);
    }

    // Return the center of the bounding box
    return (minBound + maxBound);
    
}

glm::vec3 Model::getBoundingBoxCenter() {
    glm::vec3 minBound, maxBound;
    return calculateBoundingBox(vertices, minBound, maxBound) * 0.5f;
}

void Model::loadModel() {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, MODEL_PATH.c_str())) {
        throw std::runtime_error(warn + err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            // Load position data
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            // Load texture coordinates
            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
            };

            // Load normals
            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            }
            else {

                vertex.normal = { 0.0f, 0.0f, 1.0f };
            }

            vertex.color = { 1.0f, 1.0f, 1.0f };

            if (uniqueVertices.count(vertex) == 0) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }

    glm::vec3 minBound, maxBound;
    glm::vec3 bboxCenter = calculateBoundingBox(vertices, minBound, maxBound);
}

void Model::createVertexBuffer() {
   
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer{};
    VkDeviceMemory stagingBufferMemory;
    stagingBuffer = vulkanDevice->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBufferMemory
    );

    void* data;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);

    // Create the vertex buffer
    vertexBuffer = vulkanDevice->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBufferMemory
    );

    copyBuffer(*vulkanDevice, stagingBuffer, vertexBuffer, bufferSize);

    vkDestroyBuffer(vulkanDevice->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), stagingBufferMemory, nullptr);
}

void Model::createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    stagingBuffer = vulkanDevice->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBufferMemory
    );

    void* data;
    vkMapMemory(vulkanDevice->getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(vulkanDevice->getDevice(), stagingBufferMemory);

    // Create the index buffer
    indexBuffer = vulkanDevice->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        indexBufferMemory
    );

    copyBuffer(*vulkanDevice, stagingBuffer, indexBuffer, bufferSize);

    
    vkDestroyBuffer(vulkanDevice->getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(vulkanDevice->getDevice(), stagingBufferMemory, nullptr);
}