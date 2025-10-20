#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include <mutex>
#include <queue>
#include <atomic>
#include <vector>

class Camera;
class Model;
class VulkanDevice;
class DeferredRenderer;

struct PickingRequest {
    int x;
    int y;
    bool shiftPressed;
};

class ModelSelection {
public:
    ModelSelection(VulkanDevice& device, DeferredRenderer& renderer);
    ~ModelSelection();
    
    void queuePickRequest(int x, int y, bool shiftPressed);
    void processPickingRequests(uint32_t currentFrame);
    
    uint8_t pickModelAtPosition(int x, int y, uint32_t currentFrame);
    
    void createPickingCommandPool();
    void createStagingBuffer();
    void cleanup();
    
    bool getSelected() const; 
     
    void setSelectedModelID(uint32_t id);
    void addSelectedModelID(uint32_t id);
    void removeSelectedModelID(uint32_t id);
    void clearSelection();
    bool isModelSelected(uint32_t id) const;
    
    const std::vector<uint32_t>& getSelectedModelIDsRenderThread() const;
    uint32_t getSelectedModelID() const;
    
    glm::vec3 calculateAndCacheGizmoPosition(class ResourceManager& resourceManager);
    glm::vec3 getCachedGizmoPosition(bool& valid) const;
    float getCachedGizmoScale() const;
    
    void setOutlineColor(const glm::vec3& color);
    glm::vec3 getOutlineColor() const;
    void setOutlineThickness(float thickness);
    float getOutlineThickness() const;

private:   
    VulkanDevice& vulkanDevice;
    DeferredRenderer& deferredRenderer;
    VkCommandPool pickingCommandPool;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    void* stagingBufferMapped;
    
    std::queue<PickingRequest> pickingRequestQueue;
    std::mutex pickingQueueMutex;
    
    std::vector<uint32_t> selectedModelIDs; 
    
    std::atomic<bool> cachedGizmoValid{false};
    glm::vec3 cachedGizmoPos{0.0f};
    float cachedGizmoScale{0.5f};
    float outlineThickness = 1.0f;
    glm::vec3 outlineColor = glm::vec3(pow(0.964705f, 2.2), pow(0.647058f, 2.2), pow(0.235294f, 2.2));
};
