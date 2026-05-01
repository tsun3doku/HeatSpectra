#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

#include "VulkanInstanceContext.hpp"

#include <cstring>
#include <iostream>
#include <vector>

const std::vector<const char*>& VulkanInstanceContext::validationLayers() {
    static const std::vector<const char*> layers = {
        "VK_LAYER_KHRONOS_validation",
    };
    return layers;
}

bool VulkanInstanceContext::validationEnabled() {
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

VkResult VulkanInstanceContext::createDebugUtilsMessenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* debugMessenger) {
    const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!func) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    return func(instance, createInfo, allocator, debugMessenger);
}

void VulkanInstanceContext::destroyDebugUtilsMessenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* allocator) {
    const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) {
        func(instance, debugMessenger, allocator);
    }
}

void VulkanInstanceContext::initialize(void* nativeWindowHandle, void* nativeInstanceHandle) {
    this->nativeWindowHandle = nativeWindowHandle;
    this->nativeInstanceHandle = nativeInstanceHandle;
    createInstance();
    setupDebugMessenger();
    createSurface();
}

void VulkanInstanceContext::cleanup() {
    if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE && debugMessenger != VK_NULL_HANDLE && validationEnabled()) {
        destroyDebugUtilsMessenger(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    nativeWindowHandle = nullptr;
    nativeInstanceHandle = nullptr;
}

bool VulkanInstanceContext::isValidationEnabled() const {
    return validationEnabled();
}

const std::vector<const char*>& VulkanInstanceContext::getValidationLayers() const {
    return validationLayers();
}

void VulkanInstanceContext::createInstance() {
    if (validationEnabled() && !checkValidationLayerSupport()) {
        std::cerr << "[VulkanInstanceContext] Validation layers requested, but not available" << std::endl;
        return;
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

    const std::vector<const char*> extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

    if (validationEnabled()) {
        const std::vector<const char*>& layers = validationLayers();
        createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        createInfo.ppEnabledLayerNames = layers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);

        VkValidationFeaturesEXT validationFeatures{};
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;

        VkValidationFeatureEnableEXT enabledValidationFeatures[] = {
            VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT
        };
        validationFeatures.enabledValidationFeatureCount =
            static_cast<uint32_t>(sizeof(enabledValidationFeatures) / sizeof(VkValidationFeatureEnableEXT));
        validationFeatures.pEnabledValidationFeatures = enabledValidationFeatures;
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
        std::cerr << "[VulkanInstanceContext] Failed to create instance" << std::endl;
        return;
    }
}

void VulkanInstanceContext::setupDebugMessenger() {
    if (!validationEnabled()) {
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    if (createDebugUtilsMessenger(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        std::cerr << "[VulkanInstanceContext] Failed to set up debug messenger" << std::endl;
        return;
    }
}

void VulkanInstanceContext::createSurface() {
    if (!nativeWindowHandle || !nativeInstanceHandle) {
        std::cerr << "[VulkanInstanceContext] Window is not set for surface creation" << std::endl;
        return;
    }

#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = static_cast<HWND>(nativeWindowHandle);
    createInfo.hinstance = static_cast<HINSTANCE>(nativeInstanceHandle);

    if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
        std::cerr << "[VulkanInstanceContext] Failed to create window surface" << std::endl;
        return;
    }
#else
    std::cerr << "[VulkanInstanceContext] Platform not supported yet" << std::endl;
    return;
#endif
}

std::vector<const char*> VulkanInstanceContext::getRequiredExtensions() const {
    std::vector<const char*> extensions;

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

    if (validationEnabled()) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool VulkanInstanceContext::checkValidationLayerSupport() const {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers()) {
        bool layerFound = false;
        for (const VkLayerProperties& layerProperties : availableLayers) {
            if (std::strcmp(layerName, layerProperties.layerName) == 0) {
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

void VulkanInstanceContext::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstanceContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData) {
    (void)messageSeverity;
    (void)userData;

    std::cerr << "validation layer: " << callbackData->pMessage << std::endl;
    return VK_FALSE;
}
