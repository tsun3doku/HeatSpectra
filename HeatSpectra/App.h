#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <atomic>
#include <glm/glm.hpp>

#include "VulkanWindow.h"
#include "VulkanDevice.hpp"
#include "Camera.hpp"
#include "CommandBufferManager.hpp"

class MemoryAllocator;
class ResourceManager;
class UniformBufferManager;
class DeferredRenderer;
class GBuffer;
class HeatSystem;
class ModelSelection;
class Gizmo;

class App {
public:
    App();
    ~App(); 

    void run(VulkanWindow* qtWindow);  
    void handleScrollInput(double xOffset, double yOffset);
    void handleKeyInput(Qt::Key key, bool pressed);
    void handleKeyInput(Qt::Key key, bool pressed, bool shiftPressed);
    static void mouseClickCallback(void* userPtr, int button, float mouseX, float mouseY, bool shiftPressed) {
        static_cast<App*>(userPtr)->handleMouseButton(button, mouseX, mouseY, shiftPressed);
    }
    void handleMouseMove(float mouseX, float mouseY);
    void handleMouseRelease(int button, float mouseX, float mouseY);
    void handleMouseButton(int button, float mouseX, float mouseY, bool shiftPressed);
    void applyGizmoTranslation();
    
    // Heat system control methods
    bool isHeatSystemActive() const;
    void toggleHeatSystem();
    void pauseHeatSystem();
    void resetHeatSystem();
    
    // Mesh operations
    void performRemeshing(int iterations=1, double minAngleDegrees=35.0, double maxEdgeLength=0.1, double stepSize=0.25);
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
    
    std::unique_ptr<CommandPool> uiCommandPool;      
    std::unique_ptr<CommandPool> renderCommandPool;  
    
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
    std::vector<VkFence> computeInFlightFences;
    uint32_t currentFrame;
    uint32_t frameRate;
    
    std::unique_ptr<HeatSystem> heatSystem;
    std::unique_ptr<ModelSelection> modelSelection;
    std::unique_ptr<Gizmo> gizmo;
    Camera camera;
    glm::vec3 center;
    
    double mouseX, mouseY;
    
    // Gizmo interaction state
    bool isDraggingGizmo = false;
    glm::vec3 modelStartPosition{0.0f};
    glm::vec3 accumulatedTranslation{0.0f};  // UI thread writes total translation
    glm::vec3 lastAppliedTranslation{0.0f};  // Render thread tracks what's been applied
    glm::vec3 cachedGizmoPosition{0.0f};  // Cached during drag to prevent stuttering
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
