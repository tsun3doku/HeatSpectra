#pragma once

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <array>

class CommandPool;

class VulkanDevice;
class MemoryAllocator;
class UniformBufferManager;
class Camera;
class ModelSelection;

enum class GizmoAxis {
    None = 0,
    X = 1,
    Y = 2,
    Z = 3
};

enum class GizmoMode {
    Translate,
    Rotate,
    Scale
};

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

class Gizmo {
public:
    Gizmo(VulkanDevice& vulkanDevice, MemoryAllocator& allocator, Camera& camera, VkRenderPass renderPass, VkExtent2D extent, CommandPool& renderCommandPool);
    ~Gizmo();
    
    void cleanup();
    void render(VkCommandBuffer commandBuffer, uint32_t currentFrame, const glm::vec3& position, VkExtent2D extent, float scale = 1.0f);   
    bool rayIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, float gizmoScale, GizmoAxis& hitAxis, float& hitDistance);
   
    void setMode(GizmoMode mode) { 
        currentMode = mode; 
    }
    GizmoMode getMode() const { 
        return currentMode; 
    }
    
    void setHoveredAxis(GizmoAxis axis) { 
        hoveredAxis = axis; 
    }
    GizmoAxis getHoveredAxis() const { 
        return hoveredAxis; 
    }
    
    void setActiveAxis(GizmoAxis axis) { 
        activeAxis = axis; 
    }
    GizmoAxis getActiveAxis() const { 
        return activeAxis; 
    }
    
    bool isActive() const { 
        return activeAxis != GizmoAxis::None; 
    }
    
    void startDrag(GizmoAxis axis, const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition);
    void endDrag();
    
    glm::vec3 calculateTranslationDelta(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& gizmoPosition, GizmoAxis axis);

private:
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    Camera& camera;
    CommandPool& renderCommandPool; 
    
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    
    VkBuffer arrowVertexBuffer;
    VkDeviceMemory arrowVertexBufferMemory;
    VkBuffer arrowIndexBuffer;
    VkDeviceMemory arrowIndexBufferMemory;
    uint32_t arrowIndexCount;
    
    VkBuffer coneVertexBuffer;
    VkDeviceMemory coneVertexBufferMemory;
    VkBuffer coneIndexBuffer;
    VkDeviceMemory coneIndexBufferMemory;
    uint32_t coneIndexCount;
    
    GizmoMode currentMode;
    GizmoAxis hoveredAxis;
    GizmoAxis activeAxis;
    
    glm::vec3 dragStartPos;
    glm::vec3 dragStartRayOrigin;
    glm::vec3 dragStartRayDir;
    glm::vec3 dragStartIntersection;  
    
    void createPipeline(VkRenderPass renderPass, VkExtent2D extent);
    void createGeometry();
    void createArrowGeometry();
    void createConeGeometry();
    
    void renderAxis(VkCommandBuffer commandBuffer, uint32_t currentFrame, const glm::vec3& position, VkExtent2D extent, const glm::vec3& direction, const glm::vec3& color, float scale, bool hovered);  
    bool rayCylinderIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& cylinderStart, const glm::vec3& cylinderEnd, float radius, float& distance);   
    bool rayConeIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& coneBase, const glm::vec3& coneAxis, float height, float radius, float& distance);
};
