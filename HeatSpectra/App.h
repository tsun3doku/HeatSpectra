#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <atomic>
#include <glm/glm.hpp>

#include "VulkanWindow.h"
#include "VulkanDevice.hpp"
#include "Camera.hpp"

class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class DeferredRenderer;
class GBuffer;
class HeatSystem;

class App {
public:
    App();
    ~App(); 

    void run(VulkanWindow* qtWindow);  
    void handleScrollInput(double xOffset, double yOffset);
    void handleKeyInput(Qt::Key key, bool pressed);
    
    // Heat system control methods
    bool isHeatSystemActive() const;
    void toggleHeatSystem();
    void resetHeatSystem();
    
    // Mesh operations
    void performRemeshing(int iterations=1);
    void loadModel(const std::string& modelPath);
    
    bool wireframeEnabled;
    bool commonSubdivisionEnabled;

private:
    VulkanWindow* window;
    
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    
    VulkanDevice vulkanDevice;
    std::unique_ptr<MemoryAllocator> memoryAllocator;
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<UniformBufferManager> uniformBufferManager;
    
    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    
    std::unique_ptr<DeferredRenderer> deferredRenderer;
    std::unique_ptr<GBuffer> gbuffer;
    
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkSemaphore> computeFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame;
    uint32_t frameRate;
    
    std::unique_ptr<HeatSystem> heatSystem;
    Camera camera;
    glm::vec3 center;
    
    double mouseX, mouseY;
    bool isShuttingDown;
    
    std::atomic<bool> isCameraUpdated;
    std::atomic<bool> edgeSelectionRequested;
    std::atomic<bool> isOperating;
    
    void initCore();
    void initSwapChain();
    void initRenderResources();
    void initVulkan();
    void setupCallbacks();
    void mainLoop();
    void renderLoop();
    void cleanupSwapChain();
    void recreateSwapChain();
    void cleanupRenderResources();
    void cleanupTextures();
    void cleanupScene();
    void cleanupSyncObjects();
    void cleanupCore();
    void cleanup();
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void createSwapChain();
    void createImageViews();
    void createSyncObjects();
    void drawFrame();
    
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};
