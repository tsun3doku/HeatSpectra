#pragma once

#include <vulkan/vulkan.h>

#include <vector>

class VulkanInstanceContext {
public:
    void initialize(void* nativeWindowHandle, void* nativeInstanceHandle);
    void cleanup();

    VkInstance getInstance() const {
        return instance;
    }

    VkSurfaceKHR getSurface() const {
        return surface;
    }

    bool isValidationEnabled() const;
    const std::vector<const char*>& getValidationLayers() const;

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    std::vector<const char*> getRequiredExtensions() const;
    bool checkValidationLayerSupport() const;
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const;
    static const std::vector<const char*>& validationLayers();
    static bool validationEnabled();
    static VkResult createDebugUtilsMessenger(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
        const VkAllocationCallbacks* allocator,
        VkDebugUtilsMessengerEXT* debugMessenger);
    static void destroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* allocator);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    void* nativeWindowHandle = nullptr;
    void* nativeInstanceHandle = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
};
