#pragma once

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <vector>

class CommandPool;
class GizmoController;
class ModelSelection;
class ModelRegistry;
class VulkanDevice;

namespace render {
struct SceneView;
}

struct GizmoVertex {
    glm::vec3 position;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(GizmoVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(GizmoVertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(GizmoVertex, color);

        return attributeDescriptions;
    }
};

struct GizmoPushConstants {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec3 color;
    float hovered;
};

class GizmoRenderer {
public:
    GizmoRenderer(VulkanDevice& vulkanDevice, VkRenderPass renderPass, uint32_t subpassIndex, CommandPool& renderCommandPool);
    ~GizmoRenderer();

    void createGeometry();
    void createConeGeometry();
    void createRingGeometry();
    void createPipeline(VkRenderPass renderPass, uint32_t subpassIndex);

    float calculateGizmoScale(ModelRegistry& resourceManager, const ModelSelection& modelSelection) const;

    void render(
        VkCommandBuffer commandBuffer,
        const glm::vec3& position,
        VkExtent2D extent,
        float scale,
        const render::SceneView& sceneView,
        const GizmoController& gizmoController);

    void cleanup();

private:
    struct RenderState {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        glm::vec3 position = glm::vec3(0.0f);
        float scale = 1.0f;
        float distance = 0.0f;
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 proj = glm::mat4(1.0f);
        float cameraFov = 45.0f;
    };

    float applyDistanceScaling(float baseScale, float distance, float cameraFov) const;
    float getArrowSize(float baseScale, float distance, float cameraFov) const;
    float getArrowDistance(float baseScale, float distance, float cameraFov) const;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
    void renderAxis(const RenderState& state, const glm::vec3& direction, const glm::vec3& color, bool hovered);
    void renderRotationRing(const RenderState& state, const glm::vec3& axis, const glm::vec3& color, bool hovered, float radiusMultiplier = 1.0f);

    VulkanDevice& vulkanDevice;
    CommandPool& renderCommandPool;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkBuffer coneVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory coneVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer coneIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory coneIndexBufferMemory = VK_NULL_HANDLE;
    uint32_t coneIndexCount = 0;

    VkBuffer ringVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ringVertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer ringIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ringIndexBufferMemory = VK_NULL_HANDLE;
    uint32_t ringIndexCount = 0;
};

