#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <chrono>
#include <atomic>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>

class InputManager; 

#include "VulkanWindow.h"
#include "InputManager.hpp"
#include "VulkanDevice.hpp"
#include "Camera.hpp"
#include "CommandBufferManager.hpp"

class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class FrameGraph;
class SceneRenderer;
class HeatSystem;
class ModelSelection;
class Gizmo;
class WireframeRenderer;
class LightingSystem;
class MaterialSystem;

class App {
public:
    App();
    ~App(); 

    void run(VulkanWindow* qtWindow);  
    
    bool isHeatSystemActive() const;
    bool isHeatSystemPaused() const;
    void toggleHeatSystem();
    void pauseHeatSystem();
    void resetHeatSystem();
    
    void performRemeshing(int iterations=1, double minAngleDegrees=30.0, double maxEdgeLength=0.1, double stepSize=0.25);
    void loadModel(const std::string& modelPath);
    void setPanSensitivity(float sensitivity);
    void setRenderPaused(bool paused);
    
    enum class WireframeMode { Off, Wireframe, Shaded };
    WireframeMode wireframeMode = WireframeMode::Off;
    
    bool intrinsicOverlayEnabled;
    bool heatOverlayEnabled;
    bool intrinsicNormalsEnabled;
    bool intrinsicVertexNormalsEnabled;
    bool surfelsEnabled;
    bool voronoiEnabled;
    bool pointsEnabled;
    bool contactLinesEnabled;
    bool gpuTimingOverlayEnabled;
    float intrinsicNormalLength;

    std::unique_ptr<InputManager> inputManager;  
    friend class InputManager;

private:
    VulkanWindow* window;
    
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    
    VulkanDevice vulkanDevice;
    std::unique_ptr<MemoryAllocator> memoryAllocator;
    
    std::unique_ptr<CommandPool> uiCommandPool;      
    std::unique_ptr<CommandPool> renderCommandPool;  
    
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<UniformBufferManager> uniformBufferManager;
    
    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    
    std::unique_ptr<FrameGraph> frameGraph;
    std::unique_ptr<SceneRenderer> sceneRenderer;
    
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkSemaphore> computeFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> computeInFlightFences;
    VkQueryPool computeTimingQueryPool = VK_NULL_HANDLE;
    float computeTimestampPeriod = 0.0f;
    std::vector<uint8_t> computeTimingValidFrames;
    uint32_t currentFrame;
    uint32_t frameRate;
    float overlayFps = 0.0f;
    uint32_t overlayFpsFrameCount = 0;
    bool overlayFpsInitialized = false;
    std::chrono::high_resolution_clock::time_point overlayFpsSampleStart;
    
    std::unique_ptr<HeatSystem> heatSystem;
    std::unique_ptr<ModelSelection> modelSelection;
    std::unique_ptr<Gizmo> gizmo;
    std::unique_ptr<WireframeRenderer> wireframeRenderer;
    std::unique_ptr<LightingSystem> lightingSystem;
    std::unique_ptr<MaterialSystem> materialSystem;
    Camera camera;
    glm::vec3 center;
    
    bool isShuttingDown;
    
    std::atomic<bool> isCameraUpdated;
    std::atomic<bool> edgeSelectionRequested;
    std::atomic<bool> isOperating;
    
    void initCore();
    void initSwapChain();
    void initRenderResources();
    void initVulkan();
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
    void createComputeTimingQueryPool();
    void destroyComputeTimingQueryPool();
    bool tryGetComputeGpuTimeMs(uint32_t frameIndex, float& outGpuMs) const;
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
