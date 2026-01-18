#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

class VulkanDevice;
class MemoryAllocator;
class CommandPool;

class HashGrid {
public:
    struct HashGridParams {
        glm::vec3 gridMin;          // Minimum bounds of the grid
        float cellSize;             // Size of each grid cell
        glm::ivec3 gridDimensions;  // Number of cells in each dimension
        uint32_t maxPointsPerCell;  // Maximum points that can be stored per cell
        uint32_t totalCells;        // Total number of cells
    };
    
    struct GridPoint {
        glm::vec4 position;         // World position
        glm::vec4 normal;           // Surface normal (xyz = normal, w unused)
        float temperature;          // Temperature value
        float area;                 // Area of influence
        uint32_t sourceIndex;       // Index into original buffer (heat source or receiver)
        uint32_t isHeatSource;      // 1 if heat source, 0 if receiver
    };
    
    HashGrid(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, 
             CommandPool& cmdPool, uint32_t maxFramesInFlight);
    ~HashGrid();
    
    void initialize(const glm::vec3& minBounds, const glm::vec3& maxBounds, float cellSize, uint32_t maxPointsPerCell); 
    void updateBuildDescriptors(VkBuffer sourceBuffer, VkDeviceSize sourceOffset, uint32_t frameIndex);
    
    void buildGrid(VkCommandBuffer cmdBuffer,VkBuffer sourceBuffer, uint32_t sourceCount, VkDeviceSize sourceOffset, const glm::mat4& transform, uint32_t frameIndex); 
    void recreateResources(uint32_t maxFramesInFlight);
    
    void cleanupResources();
    void cleanup();
    
    // Getters
    VkBuffer getGridBuffer() const { return gridBuffer; }
    VkDeviceSize getGridBufferOffset() const { return gridBufferOffset_; }
    VkBuffer getCellStartBuffer() const { return cellStartBuffer; }
    VkDeviceSize getCellStartBufferOffset() const { return cellStartBufferOffset_; }
    VkBuffer getCellCountBuffer() const { return cellCountBuffer; }
    VkDeviceSize getCellCountBufferOffset() const { return cellCountBufferOffset_; }
    VkBuffer getParamsBuffer() const { return paramsBuffer; }
    VkDeviceSize getParamsBufferOffset() const { return paramsBufferOffset_; }
    
    const HashGridParams& getParams() const { return params; }
    
    VkBuffer getOccupiedCellsBuffer(uint32_t frameIndex) const { 
        return frameIndex < occupiedCellsBuffers.size() ? occupiedCellsBuffers[frameIndex] : VK_NULL_HANDLE; 
    }
    VkDeviceSize getOccupiedCellsBufferOffset(uint32_t frameIndex) const { 
        return frameIndex < occupiedCellsBufferOffsets.size() ? occupiedCellsBufferOffsets[frameIndex] : 0; 
    }
    uint32_t getOccupiedCellCount(uint32_t frameIndex) const;
    
    VkBuffer getIndirectDrawBuffer(uint32_t frameIndex) const {
        return frameIndex < indirectDrawBuffers.size() ? indirectDrawBuffers[frameIndex] : VK_NULL_HANDLE;
    }
    VkDeviceSize getIndirectDrawBufferOffset(uint32_t frameIndex) const {
        return frameIndex < indirectDrawBufferOffsets.size() ? indirectDrawBufferOffsets[frameIndex] : 0;
    }
    
private:
    void createBuffers(uint32_t maxPointsTotal);
    void createBuildDescriptorSetLayout();
    void createBuildDescriptorPool(uint32_t maxFramesInFlight);
    void createBuildDescriptorSets(uint32_t maxFramesInFlight);
    void createBuildPipeline();
    
    VulkanDevice& vulkanDevice;
    MemoryAllocator& memoryAllocator;
    CommandPool& cmdPool;
    uint32_t maxFramesInFlight;
    
    HashGridParams params;
    bool isInitialized = false;
    glm::mat4 lastTransform;
    std::vector<bool> perFrameNeedsRebuild; 
    
    VkBuffer gridBuffer = VK_NULL_HANDLE;               
    VkDeviceSize gridBufferOffset_ = 0;
    
    VkBuffer cellStartBuffer = VK_NULL_HANDLE;         
    VkDeviceSize cellStartBufferOffset_ = 0;
    
    VkBuffer cellCountBuffer = VK_NULL_HANDLE;         
    VkDeviceSize cellCountBufferOffset_ = 0;
    
    VkBuffer paramsBuffer = VK_NULL_HANDLE;            
    VkDeviceSize paramsBufferOffset_ = 0;
    HashGridParams* mappedParams = nullptr;
    
    VkDescriptorPool buildDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout buildDescriptorSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> buildDescriptorSets;
    VkPipelineLayout buildPipelineLayout = VK_NULL_HANDLE;
    VkPipeline buildPipeline = VK_NULL_HANDLE;
    
    std::vector<VkBuffer> occupiedCellsBuffers;     
    std::vector<VkDeviceSize> occupiedCellsBufferOffsets;
    
    std::vector<VkBuffer> occupiedCountBuffers;      
    std::vector<VkDeviceSize> occupiedCountBufferOffsets;
    std::vector<void*> mappedOccupiedCounts;
    
    std::vector<VkBuffer> indirectDrawBuffers;
    std::vector<VkDeviceSize> indirectDrawBufferOffsets;
};
