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

enum class PickedType {
    None = 0,
    Model = 1,
    Gizmo = 2
};

enum class PickedGizmoAxis {
    None = 0,
    X = 1,
    Y = 2,
    Z = 3
};

struct PickedResult {
    PickedType type;
    uint32_t modelID;          
    PickedGizmoAxis gizmoAxis; 
    
    PickedResult() : type(PickedType::None), modelID(0), gizmoAxis(PickedGizmoAxis::None) {}
    
    bool isNone() const { 
        return type == PickedType::None; 
    }
    bool isModel() const { 
        return type == PickedType::Model; 
    }
    bool isGizmo() const { 
        return type == PickedType::Gizmo; 
    }
};

struct PickingRequest {
    int x;
    int y;
    bool shiftPressed;
    float mouseX;  
    float mouseY;
};

class ModelSelection {
public:
    ModelSelection(VulkanDevice& device, DeferredRenderer& renderer);
    ~ModelSelection();
    
    void queuePickRequest(int x, int y, bool shiftPressed, float mouseX, float mouseY);
    void processPickingRequests(uint32_t currentFrame);
    
    PickedResult pickAtPosition(int x, int y, uint32_t currentFrame);
    
    PickedResult pickImmediately(int x, int y, uint32_t currentFrame);
    
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
    
    PickedResult getLastPickedResult() const { 
        return lastPickedResult; 
    }
    PickingRequest getLastPickRequest() const { 
        return lastPickRequest; 
    }

    void clearLastPickedResult() { lastPickedResult = PickedResult(); }
        
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
    PickedResult lastPickedResult;  
    PickingRequest lastPickRequest;
    
    float outlineThickness = 1.0f;
    glm::vec3 outlineColor = glm::vec3(pow(0.964705f, 2.2), pow(0.647058f, 2.2), pow(0.235294f, 2.2));
};
