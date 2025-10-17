#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

#include <vulkan/vulkan.h>

#include "VulkanWindow.h"
#include "App.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "File_utils.h"
#include "Structs.hpp"

#include "Camera.hpp"
#include "Model.hpp"
#include "VulkanDevice.hpp"
#include "MemoryAllocator.hpp"
#include "UniformBufferManager.hpp"
#include "ResourceManager.hpp"
#include "HeatSystem.hpp"
#include "ModelSelection.hpp"
#include "DeferredRenderer.hpp"
#include "GBuffer.hpp"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <array>
#include <optional>
#include <set>
#include <thread>
#include <atomic>

uint32_t WIDTH = 960;
uint32_t HEIGHT = 540;
const int MAXFRAMESINFLIGHT = 2;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

static void scrollCallback(void* userPtr, double xOffset, double yOffset) {
    auto* app = static_cast<App*>(userPtr);
    app->handleScrollInput(xOffset, yOffset);
}

static void keyCallback(void* userPtr, Qt::Key key, bool pressed) {
    auto* app = static_cast<App*>(userPtr);
    app->handleKeyInput(key, pressed);
}

static void mouseClickCallback(void* userPtr, int button, float mouseX, float mouseY) {
    auto* app = static_cast<App*>(userPtr);
    app->handleMouseClick(button, mouseX, mouseY);
}

App::App() : wireframeEnabled(false), commonSubdivisionEnabled(false), 
             currentFrame(0), frameRate(240), mouseX(0.0), mouseY(0.0),
             isShuttingDown(false), isCameraUpdated(false), 
             edgeSelectionRequested(false), isOperating(false) {}

App::~App() = default;

void App::handleScrollInput(double xOffset, double yOffset) {
    camera.processMouseScroll(xOffset, yOffset);
}

void App::handleKeyInput(Qt::Key key, bool pressed) {
    // Only handle key press, not release
    if (!pressed) 
        return;
    
    if (key == Qt::Key_H) {
        wireframeEnabled = !wireframeEnabled;
    }
    else if (key == Qt::Key_C) {
        commonSubdivisionEnabled = !commonSubdivisionEnabled;
    }
    else if (key == Qt::Key_Space) {
        toggleHeatSystem();
    }
    else if (key == Qt::Key_P) {
        pauseHeatSystem();
    }
    else if (key == Qt::Key_R) {
        resetHeatSystem();
    }
}

void App::handleMouseClick(int button, float mouseX, float mouseY) {
    if (!modelSelection || !resourceManager) return;
    
    // Only handle left clicks
    if (button != static_cast<int>(Qt::LeftButton)) return;
    
    // Deferred GPU picking: Queue request (thread-safe)
    int x = static_cast<int>(mouseX);
    int y = static_cast<int>(mouseY);
    
    // Clamp to screen bounds (use actual render target size)
    x = std::max(0, std::min(x, static_cast<int>(swapChainExtent.width) - 1));
    y = std::max(0, std::min(y, static_cast<int>(swapChainExtent.height) - 1));
    
    // Queue the picking request - will be processed on render thread
    modelSelection->queuePickRequest(x, y);
}

bool App::isHeatSystemActive() const {
    if (heatSystem) {
        return heatSystem->getIsActive();
    }
    return false;
}

void App::toggleHeatSystem() {
    if (heatSystem) {
        bool newState = !heatSystem->getIsActive();
             
        // Pause rendering until tet mesh is ready
        if (newState && !heatSystem->getIsTetMeshReady()) {
            vkDeviceWaitIdle(vulkanDevice.getDevice());
            isOperating.store(true, std::memory_order_release);
        }
        
        heatSystem->setActive(newState);
        heatSystem->setIsPaused(false);
        
        // Reset simulation if sim is turned off
        if (!newState) {
            heatSystem->requestReset();
        }
        
        // Resume rendering
        if (newState && isOperating.load(std::memory_order_acquire)) {
            isOperating.store(false, std::memory_order_release);
        }
    }
}

void App::pauseHeatSystem() {
    if (heatSystem && heatSystem->getIsActive()) {
        heatSystem->setActive(false);
        heatSystem->setIsPaused(true); 
    }
}

void App::resetHeatSystem() {
    if (heatSystem) {
        bool wasPaused = heatSystem->getIsPaused();
        
        heatSystem->requestReset();
        heatSystem->setIsPaused(false);
        
        if (wasPaused) {
            heatSystem->setActive(true);
        }
    }
}

void App::performRemeshing(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize) {
    if (!resourceManager) {
        return;
    }
    
    // Pause rendering and wait for GPU to finish
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    isOperating.store(true, std::memory_order_release);
          
    resourceManager->performRemeshing(iterations, minAngleDegrees, maxEdgeLength, stepSize);
    
    // Resume rendering
    isOperating.store(false, std::memory_order_release);   
}

void App::loadModel(const std::string& modelPath) {
    if (!resourceManager) {
        return;
    }
    
    std::cout << "[App] Loading new model: " << modelPath << std::endl;
    
    // Flag render thread to pause
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    isOperating.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Wait for all frames to finish
    for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
        vkWaitForFences(vulkanDevice.getDevice(), 1, &inFlightFences[i], VK_TRUE, UINT64_MAX);
    }
    
    // Wait for GPU calls to finish before modifying buffers
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    
    // Reload models with new path
    resourceManager->reloadModels(modelPath);
    
    if (heatSystem) {
        // Recreate heat system for new model geometry
        heatSystem->cleanupResources();
        heatSystem->cleanup();
        heatSystem.reset();

        heatSystem = std::make_unique<HeatSystem>(
            vulkanDevice,
            *memoryAllocator,
            *resourceManager,
            *uniformBufferManager,
            MAXFRAMESINFLIGHT
        );
    }
    
    // Update camera to look at new model center
    center = resourceManager->getVisModel().getBoundingBoxCenter();
    camera.setLookAt(center);
    isCameraUpdated.store(true, std::memory_order_release);
    
    // Resume rendering
    isOperating.store(false, std::memory_order_release);
    
    std::cout << "[App] Model loaded successfully" << std::endl;
}

void App::run(VulkanWindow* qtWindow) {
    window = qtWindow;
    setupCallbacks();
    initVulkan();
    mainLoop();
    cleanup();
}

void App::initCore() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    vulkanDevice.init(instance, surface, deviceExtensions, validationLayers, enableValidationLayers);
    memoryAllocator = std::make_unique<MemoryAllocator>(vulkanDevice);
}

void App::initSwapChain() {
    createSwapChain();
    createImageViews();
}

void App::initRenderResources() {
    // Create DeferredRenderer first since it owns render pass and image views
    deferredRenderer = std::make_unique<DeferredRenderer>(
            vulkanDevice,
            swapChainImageFormat,
            swapChainExtent,
            MAXFRAMESINFLIGHT
        );

        auto renderPass = deferredRenderer->getRenderPass();

        // Create UniformBufferManager
        uniformBufferManager = std::make_unique<UniformBufferManager>(
            vulkanDevice,
            *memoryAllocator,
            camera,
            MAXFRAMESINFLIGHT
        );

        // Create ResourceManager 
        resourceManager = std::make_unique<ResourceManager>(
            vulkanDevice,
            *memoryAllocator,
            *uniformBufferManager,
            renderPass,
            camera,
            MAXFRAMESINFLIGHT);

        resourceManager->initialize();
        
        // Create ModelSelection for GPU-based picking (creates its own thread-safe command pool)
        modelSelection = std::make_unique<ModelSelection>(vulkanDevice, *deferredRenderer);

        // Create heat system
        heatSystem = std::make_unique<HeatSystem>(
            vulkanDevice,
            *memoryAllocator,
            *resourceManager,
            *uniformBufferManager,
            MAXFRAMESINFLIGHT
        );

        // Create GBuffer last since it depends on all other components
        gbuffer = std::make_unique<GBuffer>(
            vulkanDevice,
            *deferredRenderer,
            *resourceManager,
            *uniformBufferManager,
            WIDTH, HEIGHT,
            swapChainExtent,
            swapChainImageViews,
            swapChainImageFormat,
            MAXFRAMESINFLIGHT,
            wireframeEnabled
        );

        center = resourceManager->getVisModel().getBoundingBoxCenter();
        camera.setLookAt(center);
    }

void App::setupCallbacks() {
    // Set up scroll callback for camera zoom
    window->setScrollCallback(scrollCallback, this);
    // Set up key callback for controls
    window->setKeyCallback(keyCallback, this);
    // Set up mouse click callback for model selection
    window->setMouseClickCallback(mouseClickCallback, this);
}

void App::initVulkan() {
        std::cout << "Initializing Vulkan..." << std::endl;
        initCore();
        initSwapChain();
        initRenderResources();
        createSyncObjects();
    }

void App::mainLoop() {
        std::thread renderThread(&App::renderLoop, this);
        auto lastTime = std::chrono::high_resolution_clock::now();
        
        while (!window->shouldClose()) {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Get input state from Qt window
            bool wPressed = window->isKeyPressed(Qt::Key_W);
            bool sPressed = window->isKeyPressed(Qt::Key_S);
            bool aPressed = window->isKeyPressed(Qt::Key_A);
            bool dPressed = window->isKeyPressed(Qt::Key_D);
            bool qPressed = window->isKeyPressed(Qt::Key_Q);
            bool ePressed = window->isKeyPressed(Qt::Key_E);
            bool shiftPressed = window->isKeyPressed(Qt::Key_Shift);
            
            camera.processKeyInput(wPressed, sPressed, aPressed, dPressed, 
                                  qPressed, ePressed, shiftPressed, deltaTime);
            
            // Get mouse state
            bool middleButton = window->isMiddleButtonPressed();
            double mouseX, mouseY;
            window->getMousePosition(mouseX, mouseY);
            camera.processMouseMovement(middleButton, mouseX, mouseY);
            
            isCameraUpdated.store(true, std::memory_order_release);
        }
        
        // Set App's shutdown flag to true if Qt's window is closing
        isShuttingDown = true;
        renderThread.join();
    }

void App::renderLoop() {
        const double targetFrameTime = 1.0 / frameRate;
        auto lastFrameTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;

        while (!window->shouldClose() && !isShuttingDown) {
            // Skip rendering if remeshing is in progress
            if (isOperating.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            if (isCameraUpdated.load(std::memory_order_acquire)) {
                float deltaTime = std::chrono::duration<float>(
                    std::chrono::high_resolution_clock::now() - lastFrameTime).count();
                camera.update(deltaTime);
                isCameraUpdated.store(false, std::memory_order_release);
            }
            
            drawFrame();

            while (std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - lastFrameTime).count() < targetFrameTime) {
            }

            lastFrameTime = std::chrono::high_resolution_clock::now();

            // Call defragment() every 1000 frames
            frameCount++;
            if (frameCount % 1000 == 0) {
                memoryAllocator->defragment();
            }
        }

        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

void App::cleanupSwapChain() {
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    gbuffer->cleanupFramebuffers(MAXFRAMESINFLIGHT);
    deferredRenderer->cleanupImages(vulkanDevice, MAXFRAMESINFLIGHT);

    gbuffer->freeCommandBuffers();

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(vulkanDevice.getDevice(), imageView, nullptr);
        }
        swapChainImageViews.clear();
        swapChainImages.clear();
        vkDestroySwapchainKHR(vulkanDevice.getDevice(), swapChain, nullptr);
    }

void App::recreateSwapChain() {
        // Dont recreate swapchain on shutdown
        if (isShuttingDown)
            return;
            
        int width = window->width();
        int height = window->height();
        
        // Set minimum window size 
        if (width < 32 || height < 32) {
            return;
        }
        
        // Wait for valid window size 
        while (width == 0 || height == 0) {
            if (window->shouldClose())
                return;
            // Qt handles events automatically, just sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            width = window->width();
            height = window->height();
        }

        vkDeviceWaitIdle(vulkanDevice.getDevice());

        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            vkWaitForFences(vulkanDevice.getDevice(), 1, &inFlightFences[i], VK_TRUE, UINT64_MAX);
            vkResetFences(vulkanDevice.getDevice(), 1, &inFlightFences[i]);
        }

        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            vkDestroySemaphore(vulkanDevice.getDevice(), renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), computeFinishedSemaphores[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), inFlightFences[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), computeInFlightFences[i], nullptr);
        }

        cleanupSwapChain();
        heatSystem->cleanupResources();
        gbuffer->createCommandBuffers(MAXFRAMESINFLIGHT);

        createSwapChain();
        createImageViews();

        heatSystem->recreateResources(MAXFRAMESINFLIGHT);

        deferredRenderer->createImageViews(vulkanDevice, swapChainImageFormat, swapChainExtent, MAXFRAMESINFLIGHT);
        gbuffer->updateDescriptorSets(MAXFRAMESINFLIGHT);
        gbuffer->createFramebuffers(swapChainImageViews, swapChainExtent, MAXFRAMESINFLIGHT);

        createSyncObjects();

        currentFrame = 0;
    }

void App::cleanupRenderResources() {
        modelSelection->cleanup();
        deferredRenderer->cleanup(vulkanDevice);
        gbuffer->cleanup(MAXFRAMESINFLIGHT);
        uniformBufferManager->cleanup(MAXFRAMESINFLIGHT);      
        heatSystem->cleanupResources();
        heatSystem->cleanup();    
}

void App::cleanupTextures() {

    }

void App::cleanupScene() {
        resourceManager->cleanup();
    }

void App::cleanupSyncObjects() {
        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            vkDestroySemaphore(vulkanDevice.getDevice(), renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), computeFinishedSemaphores[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), inFlightFences[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), computeInFlightFences[i], nullptr);
        }
    }

void App::cleanupCore() {
        vulkanDevice.cleanup();
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
        vkDestroySurfaceKHR(instance, vulkanDevice.getSurface(), nullptr);
        vkDestroyInstance(instance, nullptr);
    }

void App::cleanup() {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
        cleanupSwapChain();
        cleanupRenderResources();
        cleanupTextures();
        cleanupScene();
        cleanupSyncObjects();
        memoryAllocator.reset();
        cleanupCore();        
    }

void App::createInstance() {
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested, but not available");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "HeatSpectra";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;  // Require Vulkan 1.3

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            debugCreateInfo.pNext = nullptr;

            VkValidationFeaturesEXT validationFeatures = {};
            validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;

            // Enable both BEST_PRACTICES and DEBUG_PRINTF
            VkValidationFeatureEnableEXT enabledValidationFeatures[] = {
                //VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
                VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT
            };
            validationFeatures.enabledValidationFeatureCount = sizeof(enabledValidationFeatures) / sizeof(VkValidationFeatureEnableEXT);
            validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;

            // Chain the validation features struct
            validationFeatures.pNext = createInfo.pNext;
            createInfo.pNext = &validationFeatures;

            debugCreateInfo.pNext = createInfo.pNext;
            createInfo.pNext = &debugCreateInfo;
        }
        else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create instance");
        }
    }

void App::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

void App::setupDebugMessenger() {
        if (!enableValidationLayers)
            return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to set up debug messenger");
        }
    }

void App::createSurface() {
#ifdef _WIN32
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = (HWND)window->getNativeWindowHandle();
        createInfo.hinstance = window->getNativeInstance();
        
        if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
#else
        throw std::runtime_error("Platform not supported yet");
#endif
    }

void App::createSwapChain() {
        // Don't create swapchain if shutting down
        if (isShuttingDown) {
            return;
        }
        
        SwapChainSupportDetails swapChainSupport = vulkanDevice.querySwapChainSupport(vulkanDevice.getPhysicalDevice(), vulkanDevice.getSurface());

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        
        // Validate extent is within bounds (surface might be invalid during shutdown)
        if (extent.width == 0 || extent.height == 0 ||
            extent.width < swapChainSupport.capabilities.minImageExtent.width ||
            extent.height < swapChainSupport.capabilities.minImageExtent.height ||
            extent.width > swapChainSupport.capabilities.maxImageExtent.width ||
            extent.height > swapChainSupport.capabilities.maxImageExtent.height) {
            return;
        }

        uint32_t imageCount = 2; // Explicitly double buffering
        if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = vulkanDevice.getSurface();

        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        QueueFamilyIndices indices = vulkanDevice.findQueueFamilies(vulkanDevice.getPhysicalDevice(), vulkanDevice.getSurface());
        uint32_t queueFamilyIndices[] = { indices.graphicsAndComputeFamily.value(), indices.presentFamily.value() };

        if (indices.graphicsAndComputeFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        // Always use identity transform (required by Qt/Windows surfaces)
        createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;


        if (vkCreateSwapchainKHR(vulkanDevice.getDevice(), &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swap chain");
        }

        vkGetSwapchainImagesKHR(vulkanDevice.getDevice(), swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(vulkanDevice.getDevice(), swapChain, &imageCount, swapChainImages.data());

        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

void App::createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());

        for (uint32_t i = 0; i < swapChainImages.size(); i++) {
            swapChainImageViews[i] = createImageView(vulkanDevice, swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

void App::createSyncObjects() {
        imageAvailableSemaphores.resize(MAXFRAMESINFLIGHT);
        renderFinishedSemaphores.resize(MAXFRAMESINFLIGHT);
        computeFinishedSemaphores.resize(MAXFRAMESINFLIGHT);
        inFlightFences.resize(MAXFRAMESINFLIGHT);
        computeInFlightFences.resize(MAXFRAMESINFLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            if (vkCreateSemaphore(vulkanDevice.getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(vulkanDevice.getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(vulkanDevice.getDevice(), &semaphoreInfo, nullptr, &computeFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(vulkanDevice.getDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS ||
                vkCreateFence(vulkanDevice.getDevice(), &fenceInfo, nullptr, &computeInFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create synchronization objects for a frame");
            }
        }
    }

void App::drawFrame() {
        // Skip rendering if shutting down or operating
        if (isShuttingDown || isOperating.load(std::memory_order_acquire)) {
            return;
        }
        
        // Wait for previous frame's fence (ensures GPU finished with this frame slot)
        vkWaitForFences(vulkanDevice.getDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        // Process pick results from previous frame (now safe - GPU work complete)
        if (modelSelection) {
            modelSelection->processPickingRequests(currentFrame);
        }

        // Get next image
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(vulkanDevice.getDevice(), swapChain, UINT64_MAX,
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreateSwapChain();
            return;
        }
        else if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        VkCommandBuffer commandBuffer = gbuffer->getCommandBuffers()[currentFrame];
        vkResetCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        vkResetFences(vulkanDevice.getDevice(), 1, &inFlightFences[currentFrame]);

        UniformBufferObject ubo{};
        uniformBufferManager->updateUniformBuffer(swapChainExtent, currentFrame, ubo);
        GridUniformBufferObject gridUbo{};
        uniformBufferManager->updateGridUniformBuffer(currentFrame, ubo, gridUbo);
        LightUniformBufferObject lightUbo{};
        uniformBufferManager->updateLightUniformBuffer(currentFrame, lightUbo);

        VkCommandBuffer computeCommandBuffer = heatSystem->getComputeCommandBuffers()[currentFrame];
        vkResetCommandBuffer(computeCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

        VkCommandBufferBeginInfo computeBeginInfo{};
        computeBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        computeBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        // Get arrow key state for heat source control
        bool upPressed = window->isKeyPressed(Qt::Key_Up);
        bool downPressed = window->isKeyPressed(Qt::Key_Down);
        bool leftPressed = window->isKeyPressed(Qt::Key_Left);
        bool rightPressed = window->isKeyPressed(Qt::Key_Right);
        
        heatSystem->update(upPressed, downPressed, leftPressed, rightPressed, ubo, WIDTH, HEIGHT);
        if (heatSystem->getIsActive() && heatSystem->getIsTetMeshReady()) {
            // Wait for previous compute to finish
            vkWaitForFences(vulkanDevice.getDevice(), 1, &computeInFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
            vkResetFences(vulkanDevice.getDevice(), 1, &computeInFlightFences[currentFrame]);

            heatSystem->recordComputeCommands(computeCommandBuffer, currentFrame);

            VkSubmitInfo computeSubmitInfo{};
            computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            computeSubmitInfo.commandBufferCount = 1;
            computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;
            computeSubmitInfo.signalSemaphoreCount = 1;
            computeSubmitInfo.pSignalSemaphores = &computeFinishedSemaphores[currentFrame];

            vkQueueSubmit(vulkanDevice.getComputeQueue(), 1, &computeSubmitInfo, computeInFlightFences[currentFrame]);
        }

        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;

        if (heatSystem->getIsActive() && heatSystem->getIsTetMeshReady()) {
            waitSemaphores = { computeFinishedSemaphores[currentFrame], imageAvailableSemaphores[currentFrame] };
            waitStages = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        }
        else {
            waitSemaphores = { imageAvailableSemaphores[currentFrame] };
            waitStages = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        }

        // Graphics pass
        gbuffer->recordCommandBuffer(*resourceManager, *heatSystem, *modelSelection, swapChainImageViews, currentFrame, imageIndex, MAXFRAMESINFLIGHT, swapChainExtent, wireframeEnabled, commonSubdivisionEnabled);

        VkSubmitInfo graphicsSubmitInfo{};
        graphicsSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        graphicsSubmitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        graphicsSubmitInfo.pWaitSemaphores = waitSemaphores.data();
        graphicsSubmitInfo.pWaitDstStageMask = waitStages.data();
        graphicsSubmitInfo.commandBufferCount = 1;
        graphicsSubmitInfo.pCommandBuffers = &commandBuffer;
        graphicsSubmitInfo.signalSemaphoreCount = 1;
        graphicsSubmitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];

        result = vkQueueSubmit(vulkanDevice.getGraphicsQueue(), 1, &graphicsSubmitInfo, inFlightFences[currentFrame]);
        if (result != VK_SUCCESS) {
            vkDeviceWaitIdle(vulkanDevice.getDevice());
            recreateSwapChain();
            return;
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];
        VkSwapchainKHR swapChains[] = { swapChain };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(vulkanDevice.getPresentQueue(), &presentInfo);
        if (result != VK_SUCCESS) {
            vkDeviceWaitIdle(vulkanDevice.getDevice());
            recreateSwapChain();
            return;
        }

        currentFrame = (currentFrame + 1) % MAXFRAMESINFLIGHT;
    }

static VkShaderModule createShaderModule(VulkanDevice& vulkanDevice, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vulkanDevice.getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }

    return shaderModule;
}

VkSurfaceFormatKHR App::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

VkPresentModeKHR App::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) { // VK_PRESENT_MODE_MAILBOX_KHR or VK_PRESENT_MODE_FIFO_KHR
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

VkExtent2D App::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        // Validate capabilities aren't corrupted
        if (capabilities.minImageExtent.width == 0 || capabilities.minImageExtent.height == 0) {
            // Surface is invalid/destroyed, return dummy extent (will be caught by shutdown check)
            return {0, 0};
        }
        
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        else {
            int width = window->width();
            int height = window->height();

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

std::vector<const char*> App::getRequiredExtensions() {
        std::vector<const char*> extensions;
        
        // Platform specific surface extension
#ifdef _WIN32
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined(__APPLE__)
        extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        extensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#endif

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

bool App::checkValidationLayerSupport() {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : validationLayers) {
            bool layerFound = false;

            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) {
                return false;
            }
        }

        return true;
    }

VKAPI_ATTR VkBool32 VKAPI_CALL App::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}