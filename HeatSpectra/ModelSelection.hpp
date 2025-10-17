#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include <mutex>
#include <queue>
#include <atomic>

class Camera;
class Model;
class VulkanDevice;
class DeferredRenderer;

struct PickingRequest {
    int x;
    int y;
};

class ModelSelection {
public:
    ModelSelection(VulkanDevice& device, DeferredRenderer& renderer);
    ~ModelSelection();
    
    void queuePickRequest(int x, int y);                                        // Call from UI thread
    void processPickingRequests(uint32_t currentFrame);     // Call from render thread
    
    uint8_t pickModelAtPosition(int x, int y, uint32_t currentFrame);
    
    void createPickingCommandPool();
    void createStagingBuffer();
    void cleanup();
    
    void setSelected(bool selected) { 
        isSelected = selected; 
    }
    bool getSelected() const { 
        return isSelected; 
    }
    void toggleSelection() { 
        isSelected = !isSelected; 
    }
    
    void setSelectedModelID(uint32_t id) {
        selectedModelID = id;
    }
    uint32_t getSelectedModelID() const {
        return selectedModelID;
    }
    
    void setOutlineColor(const glm::vec3& color) { 
        outlineColor = color; 
    }
    glm::vec3 getOutlineColor() const { 
        return outlineColor; 
    }
    
    void setOutlineThickness(float thickness) { 
        outlineThickness = thickness; 
    }
    float getOutlineThickness() const { 
        return outlineThickness; 
    }

private:   
    VulkanDevice& vulkanDevice;
    DeferredRenderer& deferredRenderer;
    VkCommandPool pickingCommandPool;
    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    void* stagingBufferMapped;
    
    // Deferred picking queue
    std::queue<PickingRequest> pickingRequestQueue;
    std::mutex pickingQueueMutex;

    bool isSelected = false;
    uint32_t selectedModelID = 0;
    float outlineThickness = 1.5f;
    glm::vec3 outlineColor = glm::vec3(pow(0.964705f, 2.2), pow(0.647058f, 2.2), pow(0.235294f, 2.2));
};
