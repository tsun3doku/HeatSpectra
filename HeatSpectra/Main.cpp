#define GLFW_INCLUDE_VULKAN                       
#include <GLFW/glfw3.h>                                                   
                      
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "CommandBufferManager.hpp"
#include "Model.hpp"
#include "Structs.hpp"
#include "VulkanImage.hpp"
#include "MemoryAllocator.hpp"
#include "UniformBufferManager.hpp" 
#include "Camera.hpp"
#include "File_utils.h" 
//#include "HDR.hpp"
#include "Grid.hpp"
#include "GBuffer.hpp" 
#include "HeatSource.hpp"
#include "HeatSystem.hpp"

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

VkResult static CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void static DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

class App {
public:
    App() {}
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window{};

    VkInstance instance{};
    VkDebugUtilsMessengerEXT debugMessenger{};
    VkSurfaceKHR surface{};

    VulkanDevice vulkanDevice;
    std::unique_ptr<MemoryAllocator> memoryAllocator;

    VkSwapchainKHR swapChain{};
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat{};
    VkExtent2D swapChainExtent{};
    std::vector<VkImageView> swapChainImageViews;

    Model simModel;
    Model visModel;
    Model heatModel;
    //HDR hdr; 
    GBuffer gbuffer; 
    Grid grid;
    HeatSource heatSource;
    HeatSystem heatSystem;

    UniformBufferManager uniformBufferManager;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkSemaphore> computeFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;
    uint32_t frameRate = 240;

    Camera camera;
    
    bool framebufferResized = false;
    std::atomic<bool> isCameraUpdated{ false };

    glm::vec3 center;

    void initWindow() {
        glfwInit();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwSwapInterval(0);

        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);   

    }

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
        app->framebufferResized = true;   

        WIDTH = width;
        HEIGHT = height;
    }

    static void scroll_callback(GLFWwindow* window, double xOffset, double yOffset) {
        auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(window));
        app->camera.processMouseScroll(xOffset, yOffset);
    }

    void initCore() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        vulkanDevice.init(instance, surface, deviceExtensions, validationLayers, enableValidationLayers);
        memoryAllocator = std::make_unique<MemoryAllocator>(vulkanDevice);
    }

    void initSwapChain() {
        createSwapChain();
        createImageViews();
    }

    void initScene() {
        // Initialize surface model
        simModel.init(vulkanDevice, *memoryAllocator, MODEL_PATH);

        // Create subdivided version for visualization
        visModel = simModel;
        visModel.setSubdivisionLevel(2);
        visModel.subdivide();

        // Recreate buffers with subdivided size
        visModel.recreateBuffers();

        // Initialize heat source model
        heatModel.init(vulkanDevice, *memoryAllocator, HEATSOURCE_PATH);

        heatSystem.generateTetrahedralMesh(simModel);

        center = simModel.getBoundingBoxCenter();
        camera.setLookAt(center);
    }

    void initRenderResources() {
        uniformBufferManager.init(vulkanDevice, swapChainExtent, MAXFRAMESINFLIGHT);

        heatSource.init(vulkanDevice, *memoryAllocator, heatModel, MAXFRAMESINFLIGHT);
        heatSystem.init(vulkanDevice, uniformBufferManager, simModel, visModel, heatModel, heatSource, camera, MAXFRAMESINFLIGHT);

        gbuffer.init(vulkanDevice, *memoryAllocator, uniformBufferManager, visModel, heatModel, grid, heatSource, heatSystem, WIDTH, HEIGHT, swapChainExtent, swapChainImageViews, swapChainImageFormat, MAXFRAMESINFLIGHT);    
    }

    void initVulkan() {
        std::cout << "Initializing Vulkan..." << std::endl;
        initCore();
        initSwapChain();
        initScene();
        initRenderResources();
        createSyncObjects();
    }

    void mainLoop() {
        std::thread renderThread(&App::renderLoop, this);
        UniformBufferObject ubo{};
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            camera.processKeyInput(window);
            camera.processMouseMovement(window);
            isCameraUpdated.store(true, std::memory_order_release);
        }

        renderThread.join();
    }

    void renderLoop() {
        const double targetFrameTime = 1.0 / frameRate;
        auto lastFrameTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;

        while (!glfwWindowShouldClose(window)) {
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
                memoryAllocator->defragment(); // Call defragment on the memory allocator
            }
        }

        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

    void cleanupSwapChain() {    
        vkDeviceWaitIdle(vulkanDevice.getDevice());

        gbuffer.cleanupFramebuffers(vulkanDevice, MAXFRAMESINFLIGHT);       
        gbuffer.cleanupImages(vulkanDevice, MAXFRAMESINFLIGHT);             
        
        for (auto imageView : swapChainImageViews) {
            std::cout << "Destroyed swapchain: " << imageView << std::endl;
            vkDestroyImageView(vulkanDevice.getDevice(), imageView, nullptr);
        }

        if (gbuffer.getCommandBuffers().size() != MAXFRAMESINFLIGHT) {
            gbuffer.freeCommandBuffers(vulkanDevice);
            gbuffer.createCommandBuffers(vulkanDevice, MAXFRAMESINFLIGHT);
        }
        swapChainImageViews.clear();
        swapChainImages.clear();
        vkDestroySwapchainKHR(vulkanDevice.getDevice(), swapChain, nullptr);
    }

    void recreateSwapChain() {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        while (width == 0 || height == 0) {
            if (glfwWindowShouldClose(window))
                return;
            glfwWaitEvents();
            glfwGetFramebufferSize(window, &width, &height);
           
        }

        vkDeviceWaitIdle(vulkanDevice.getDevice());

        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            vkWaitForFences(vulkanDevice.getDevice(), 1, &inFlightFences[i], VK_TRUE, UINT64_MAX);
            vkResetFences(vulkanDevice.getDevice(), 1, &inFlightFences[i]);
        }

        //vkResetCommandPool(vulkanDevice.getDevice(), vulkanDevice.getCommandPool(), VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            vkDestroySemaphore(vulkanDevice.getDevice(), renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), computeFinishedSemaphores[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), inFlightFences[i], nullptr);
        }

        cleanupSwapChain();
        heatSystem.cleanupResources(vulkanDevice);
        heatSource.cleanup(vulkanDevice);

        createSwapChain();
        createImageViews();

        heatSource.init(vulkanDevice, *memoryAllocator, heatModel, MAXFRAMESINFLIGHT);
        //heatSystem.init(vulkanDevice, uniformBufferManager, simModel, visModel, heatModel, heatSource, camera, MAXFRAMESINFLIGHT);
        heatSystem.recreateResources(vulkanDevice, MAXFRAMESINFLIGHT, heatSource);

        gbuffer.createImageViews(vulkanDevice, swapChainExtent, MAXFRAMESINFLIGHT);
        gbuffer.updateDescriptorSets(vulkanDevice, MAXFRAMESINFLIGHT);
        gbuffer.createFramebuffers(vulkanDevice, grid, swapChainImageViews, swapChainExtent, MAXFRAMESINFLIGHT);

        createSyncObjects();
        
        currentFrame = 0;
    }

    void cleanupRenderResources() {
        uniformBufferManager.cleanup(MAXFRAMESINFLIGHT);
        gbuffer.cleanup(vulkanDevice, MAXFRAMESINFLIGHT);
    }

    void cleanupTextures() {
        
    }

    void cleanupScene() {
        simModel.cleanup();
        visModel.cleanup();
        heatModel.cleanup();
    }

    void cleanupSyncObjects() {
        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            vkDestroySemaphore(vulkanDevice.getDevice(), renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), imageAvailableSemaphores[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), inFlightFences[i], nullptr);
        }
    }

    void cleanupCore() {
        vulkanDevice.cleanup();
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
        vkDestroySurfaceKHR(instance, vulkanDevice.getSurface(), nullptr);
        vkDestroyInstance(instance, nullptr);
    }

    void cleanup() {
        cleanupSwapChain();
        cleanupRenderResources();
        cleanupTextures();
        cleanupScene();
        cleanupSyncObjects();
        cleanupCore();

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void createInstance() {
        if (enableValidationLayers && !checkValidationLayerSupport()) {
            throw std::runtime_error("Validation layers requested, but not available");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "HeatSpectra";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

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
                VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
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


    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
    }

    void setupDebugMessenger() {
        if (!enableValidationLayers)
            return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        populateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("Failed to set up debug messenger");
        }
    }

    void createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
    }

    void createSwapChain() {
        SwapChainSupportDetails swapChainSupport = vulkanDevice.querySwapChainSupport(vulkanDevice.getPhysicalDevice(), vulkanDevice.getSurface());

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

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

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
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

    void createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());

        for (uint32_t i = 0; i < swapChainImages.size(); i++) {
            swapChainImageViews[i] = createImageView(vulkanDevice, swapChainImages[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    void createSyncObjects() {
        imageAvailableSemaphores.resize(MAXFRAMESINFLIGHT);
        renderFinishedSemaphores.resize(MAXFRAMESINFLIGHT);
        computeFinishedSemaphores.resize(MAXFRAMESINFLIGHT);
        inFlightFences.resize(MAXFRAMESINFLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            if (vkCreateSemaphore(vulkanDevice.getDevice(), &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(vulkanDevice.getDevice(), &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(vulkanDevice.getDevice(), &semaphoreInfo, nullptr, &computeFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(vulkanDevice.getDevice(), &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create synchronization objects for a frame");
            }
        }
    }
  
    void drawFrame() {
        // Wait for previous frame's fence
        vkWaitForFences(vulkanDevice.getDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        // Get next image
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(vulkanDevice.getDevice(), swapChain, UINT64_MAX,
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
            return;
        }
        else if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        VkCommandBuffer commandBuffer = gbuffer.getCommandBuffers()[currentFrame];
        vkResetCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        vkResetFences(vulkanDevice.getDevice(), 1, &inFlightFences[currentFrame]);
        
        UniformBufferObject ubo{};
        uniformBufferManager.updateUniformBuffer(currentFrame, camera, ubo);
        GridUniformBufferObject gridUbo{};
        uniformBufferManager.updateGridUniformBuffer(currentFrame, camera, ubo, gridUbo);
        LightUniformBufferObject lightUbo{};
        uniformBufferManager.updateLightUniformBuffer(currentFrame, camera, lightUbo);

        VkCommandBuffer computeCommandBuffer = heatSystem.getComputeCommandBuffers()[currentFrame];
        vkResetCommandBuffer(computeCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        
        VkCommandBufferBeginInfo computeBeginInfo{};
        computeBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        computeBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        heatSystem.update(vulkanDevice, window, ubo, WIDTH, HEIGHT);
        heatSystem.recordComputeCommands(computeCommandBuffer, currentFrame);

        VkSubmitInfo computeSubmitInfo{};
        computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        computeSubmitInfo.commandBufferCount = 1;
        computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;
        computeSubmitInfo.signalSemaphoreCount = 1;
        computeSubmitInfo.pSignalSemaphores = &computeFinishedSemaphores[currentFrame];

        vkQueueSubmit(vulkanDevice.getComputeQueue(), 1, &computeSubmitInfo, VK_NULL_HANDLE);
        heatSystem.swapBuffers();

        // Graphics pass
        gbuffer.recordCommandBuffer(vulkanDevice, visModel, swapChainImageViews, imageIndex, MAXFRAMESINFLIGHT, swapChainExtent);

        std::array<VkPipelineStageFlags, 2> waitStages = {
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,              // For compute semaphore
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }; // For image available semaphore
        std::array<VkSemaphore, 2> waitSemaphores = {computeFinishedSemaphores[currentFrame],imageAvailableSemaphores[currentFrame]};

        VkSubmitInfo graphicsSubmitInfo{};
        graphicsSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        graphicsSubmitInfo.waitSemaphoreCount = 2;
        graphicsSubmitInfo.pWaitSemaphores = waitSemaphores.data(); // Wait for compute and image acquisition
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

    VkShaderModule createShaderModule(const std::vector<char>& code) {
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
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) { // VK_PRESENT_MODE_MAILBOX_KHR or VK_PRESENT_MODE_FIFO_KHR
                return availablePresentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        else {
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);

            VkExtent2D actualExtent = {
                static_cast<uint32_t>(width),
                static_cast<uint32_t>(height)
            };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions;
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        
        return extensions;
    }
    
    bool checkValidationLayerSupport() {
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

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, 
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

        return VK_FALSE;
    }

};

int main() {
    App app;
  
    try {
        app.run();
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}