#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

#include <vulkan/vulkan.h>

#include "VulkanWindow.h"
#include "App.h"
#include "WireframeRenderer.hpp"

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
#include "Grid.hpp"
#include "HeatSystem.hpp"
#include "ModelSelection.hpp"
#include "Gizmo.hpp"
#include "FrameGraph.hpp"
#include "SceneRenderer.hpp"
#include "VulkanImage.hpp"
#include "CommandBufferManager.hpp"
#include "InputManager.hpp"
#include "LightingSystem.hpp"
#include "MaterialSystem.hpp"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cctype>
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

namespace {

void registerDefaultFrameGraphDesc(FrameGraph& frameGraph) {
    frameGraph.clearGraphDesc();

    auto addImageResource = [&](const char* name,
        VkFormat format,
        bool useSwapchainFormat,
        VkSampleCountFlagBits samples,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspect,
        VkAttachmentLoadOp loadOp,
        VkAttachmentStoreOp storeOp,
        VkAttachmentLoadOp stencilLoadOp,
        VkAttachmentStoreOp stencilStoreOp,
        VkImageLayout initialLayout,
        VkImageLayout finalLayout,
        fg::ResourceLifetime lifetime = fg::ResourceLifetime::Transient) {
        fg::ResourceDesc desc{};
        desc.name = name;
        desc.type = fg::ResourceType::Image2D;
        desc.lifetime = lifetime;
        desc.format = format;
        desc.useSwapchainFormat = useSwapchainFormat;
        desc.samples = samples;
        desc.imageUsage = usage;
        desc.viewAspect = aspect;
        desc.loadOp = loadOp;
        desc.storeOp = storeOp;
        desc.stencilLoadOp = stencilLoadOp;
        desc.stencilStoreOp = stencilStoreOp;
        desc.initialLayout = initialLayout;
        desc.finalLayout = finalLayout;
        desc.finalOutput = (lifetime == fg::ResourceLifetime::External);
        return frameGraph.addResourceDesc(std::move(desc));
    };

    const uint32_t resAlbedoMSAA = addImageResource("AlbedoMSAA", VK_FORMAT_R8G8B8A8_UNORM, false, VK_SAMPLE_COUNT_8_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const uint32_t resNormalMSAA = addImageResource("NormalMSAA", VK_FORMAT_R16G16B16A16_SFLOAT, false, VK_SAMPLE_COUNT_8_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const uint32_t resPositionMSAA = addImageResource("PositionMSAA", VK_FORMAT_R16G16B16A16_SFLOAT, false, VK_SAMPLE_COUNT_8_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const uint32_t resDepthMSAA = addImageResource("DepthMSAA", VK_FORMAT_D32_SFLOAT_S8_UINT, false, VK_SAMPLE_COUNT_8_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    const uint32_t resAlbedoResolve = addImageResource("AlbedoResolve", VK_FORMAT_R8G8B8A8_UNORM, false, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const uint32_t resNormalResolve = addImageResource("NormalResolve", VK_FORMAT_R16G16B16A16_SFLOAT, false, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const uint32_t resPositionResolve = addImageResource("PositionResolve", VK_FORMAT_R16G16B16A16_SFLOAT, false, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const uint32_t resDepthResolve = addImageResource("DepthResolve", VK_FORMAT_D32_SFLOAT_S8_UINT, false, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    const uint32_t resLightingMSAA = addImageResource("LightingMSAA", VK_FORMAT_UNDEFINED, true, VK_SAMPLE_COUNT_8_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const uint32_t resLineMSAA = addImageResource("LineMSAA", VK_FORMAT_UNDEFINED, true, VK_SAMPLE_COUNT_8_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const uint32_t resLineResolve = addImageResource("LineResolve", VK_FORMAT_UNDEFINED, true, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const uint32_t resLightingResolve = addImageResource("LightingResolve", VK_FORMAT_UNDEFINED, true, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    const uint32_t resSwapchain = addImageResource("Swapchain", VK_FORMAT_UNDEFINED, true, VK_SAMPLE_COUNT_1_BIT,
        0,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        fg::ResourceLifetime::External);

    const uint32_t resSurfaceMSAA = addImageResource("SurfaceMSAA", VK_FORMAT_UNDEFINED, true, VK_SAMPLE_COUNT_8_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const uint32_t resSurfaceResolve = addImageResource("SurfaceResolve", VK_FORMAT_UNDEFINED, true, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    fg::PassDesc geometryPass{};
    geometryPass.name = "GeometryPass";
    geometryPass.colors = {
        fg::AttachmentRef{resAlbedoMSAA},
        fg::AttachmentRef{resNormalMSAA},
        fg::AttachmentRef{resPositionMSAA},
    };
    geometryPass.resolves = {
        fg::AttachmentRef{resAlbedoResolve},
        fg::AttachmentRef{resNormalResolve},
        fg::AttachmentRef{resPositionResolve},
    };
    geometryPass.depthStencil = fg::AttachmentRef{resDepthMSAA};
    geometryPass.uses = {
        {resAlbedoMSAA, fg::UsageType::ColorAttachment, true},
        {resNormalMSAA, fg::UsageType::ColorAttachment, true},
        {resPositionMSAA, fg::UsageType::ColorAttachment, true},
        {resAlbedoResolve, fg::UsageType::ColorAttachment, true},
        {resNormalResolve, fg::UsageType::ColorAttachment, true},
        {resPositionResolve, fg::UsageType::ColorAttachment, true},
        {resDepthMSAA, fg::UsageType::DepthStencilAttachment, true},
    };
    frameGraph.addPassDesc(std::move(geometryPass));

    fg::PassDesc lightingPass{};
    lightingPass.name = "LightingPass";
    fg::AttachmentRef depthResolveInput{resDepthResolve};
    depthResolveInput.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthResolveInput.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    fg::AttachmentRef stencilInput{resDepthMSAA};
    stencilInput.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    stencilInput.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    lightingPass.inputs = {
        fg::AttachmentRef{resAlbedoResolve},
        fg::AttachmentRef{resNormalResolve},
        fg::AttachmentRef{resPositionResolve},
        depthResolveInput,
        stencilInput,
    };
    lightingPass.colors = { fg::AttachmentRef{resLightingMSAA} };
    lightingPass.resolves = { fg::AttachmentRef{resLightingResolve} };
    fg::AttachmentRef lightingDepth{resDepthMSAA};
    lightingDepth.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    lightingDepth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    lightingPass.depthStencil = lightingDepth;
    lightingPass.depthReadOnly = true;
    lightingPass.uses = {
        {resAlbedoResolve, fg::UsageType::InputAttachment, false},
        {resNormalResolve, fg::UsageType::InputAttachment, false},
        {resPositionResolve, fg::UsageType::InputAttachment, false},
        {resDepthResolve, fg::UsageType::InputAttachment, false},
        {resDepthMSAA, fg::UsageType::DepthStencilAttachment, false},
        {resLightingMSAA, fg::UsageType::ColorAttachment, true},
        {resLightingResolve, fg::UsageType::ColorAttachment, true},
    };
    frameGraph.addPassDesc(std::move(lightingPass));

    fg::PassDesc overlayPass{};
    overlayPass.name = "OverlayPass";
    overlayPass.colors = {
        fg::AttachmentRef{resSurfaceMSAA},
        fg::AttachmentRef{resLineMSAA},
    };
    overlayPass.resolves = {
        fg::AttachmentRef{resSurfaceResolve},
        fg::AttachmentRef{resLineResolve},
    };
    fg::AttachmentRef overlayDepth{resDepthMSAA};
    overlayDepth.layout = VK_IMAGE_LAYOUT_GENERAL;
    overlayPass.depthStencil = overlayDepth;
    fg::AttachmentRef overlayDepthResolve{resDepthResolve};
    overlayDepthResolve.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    overlayDepthResolve.layout = VK_IMAGE_LAYOUT_GENERAL;
    overlayPass.depthResolve = overlayDepthResolve;
    overlayPass.uses = {
        {resDepthMSAA, fg::UsageType::DepthStencilAttachment, true},
        {resDepthResolve, fg::UsageType::DepthStencilAttachment, true},
        {resSurfaceMSAA, fg::UsageType::ColorAttachment, true},
        {resLineMSAA, fg::UsageType::ColorAttachment, true},
        {resSurfaceResolve, fg::UsageType::ColorAttachment, true},
        {resLineResolve, fg::UsageType::ColorAttachment, true},
    };
    frameGraph.addPassDesc(std::move(overlayPass));

    fg::PassDesc blendPass{};
    blendPass.name = "BlendPass";
    blendPass.inputs = {
        fg::AttachmentRef{resSurfaceResolve},
        fg::AttachmentRef{resLineResolve},
        fg::AttachmentRef{resLightingResolve},
        fg::AttachmentRef{resAlbedoResolve},
    };
    blendPass.colors = { fg::AttachmentRef{resSwapchain} };
    blendPass.uses = {
        {resSurfaceResolve, fg::UsageType::InputAttachment, false},
        {resLineResolve, fg::UsageType::InputAttachment, false},
        {resLightingResolve, fg::UsageType::InputAttachment, false},
        {resAlbedoResolve, fg::UsageType::InputAttachment, false},
        {resSwapchain, fg::UsageType::Present, true},
    };
    frameGraph.addPassDesc(std::move(blendPass));
}

} // namespace

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

App::App() : intrinsicOverlayEnabled(false), heatOverlayEnabled(false), intrinsicNormalsEnabled(false), intrinsicVertexNormalsEnabled(false), surfelsEnabled(false), voronoiEnabled(false), pointsEnabled(false), contactLinesEnabled(false), gpuTimingOverlayEnabled(false), intrinsicNormalLength(0.05f),
             currentFrame(0), frameRate(240),
             isShuttingDown(false), isCameraUpdated(false), 
             edgeSelectionRequested(false), isOperating(false) {}

App::~App() = default;

void App::setPanSensitivity(float sensitivity) { 
    camera.panSensitivity = sensitivity; 
}

void App::setRenderPaused(bool paused) {
    if (paused) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
        isOperating.store(true, std::memory_order_release);
    } else {
        isOperating.store(false, std::memory_order_release);
    }
}

bool App::isHeatSystemActive() const {
    if (heatSystem) {
        return heatSystem->getIsActive();
    }
    return false;
}

bool App::isHeatSystemPaused() const {
    if (heatSystem) {
        return heatSystem->getIsPaused();
    }
    return false;
}

void App::toggleHeatSystem() {
    if (heatSystem) {
        const bool isActive = heatSystem->getIsActive();
        const bool isPaused = heatSystem->getIsPaused();

        if (isActive && isPaused) {
            heatSystem->setIsPaused(false);
            return;
        }

        bool newState = !isActive;
        
        // Cant activate heat system until both models are remeshed
        if (newState && !resourceManager->areRequiredModelsRemeshed()) {
            std::cerr << "[App] Cannot activate heatsystem\n" << std::endl;
            std::cerr << " Remesh all models before activating heatsystem" << std::endl;
            return; 
        }
        
        // Only pause rendering when turning on and tet mesh is being built
        if (newState && !heatSystem->getIsVoronoiReady()) {
            vkDeviceWaitIdle(vulkanDevice.getDevice());
            isOperating.store(true, std::memory_order_release);
        }
        
        heatSystem->setActive(newState);
        heatSystem->setIsPaused(false);

        if (newState && sceneRenderer && resourceManager && uniformBufferManager) {
            Model* heatModel = &resourceManager->getHeatModel();
            iODT* remesher = resourceManager->getRemesherForModel(heatModel);
            if (remesher) {
                sceneRenderer->updateDescriptorSetsForModel(heatModel, remesher, *uniformBufferManager, MAXFRAMESINFLIGHT);
                sceneRenderer->updateNormalsDescriptorSetsForModel(heatModel, remesher, *uniformBufferManager, MAXFRAMESINFLIGHT);
                sceneRenderer->updateVertexNormalsDescriptorSetsForModel(heatModel, remesher, *uniformBufferManager, MAXFRAMESINFLIGHT);
            }
        }
        
        // Reset simulation if sim is turned off
        if (!newState) {
            heatSystem->requestReset();
        }
        
        // Resume rendering after tet mesh is completed
        if (newState && isOperating.load(std::memory_order_acquire)) {
            isOperating.store(false, std::memory_order_release);
        }
    }
}

void App::pauseHeatSystem() {
    if (heatSystem && heatSystem->getIsActive() && !heatSystem->getIsPaused()) {
        heatSystem->setIsPaused(true); 
    }
}

void App::resetHeatSystem() {
    if (heatSystem) {
        // Check before reactivating
        if (!resourceManager->areRequiredModelsRemeshed()) {
            std::cerr << "[App] Cannot reset/reactivate heat system: Both models must be remeshed!" << std::endl;
            return;
        }
        
        bool wasPaused = heatSystem->getIsPaused();
        
        heatSystem->requestReset();
        heatSystem->setIsPaused(false);
        
        if (wasPaused) {
            heatSystem->setActive(true);
        }
    }
}

void App::performRemeshing(int iterations, double minAngleDegrees, double maxEdgeLength, double stepSize) {
    if (!resourceManager || !modelSelection) {
        return;
    }
    
    // Pause rendering and wait for GPU to finish
    vkDeviceWaitIdle(vulkanDevice.getDevice());
    isOperating.store(true, std::memory_order_release);
    
    // Get the selected model from ModelSelection
    uint32_t selectedModelID = modelSelection->getSelectedModelID();
    Model* targetModel = resourceManager->getModelByID(selectedModelID);
    
    // If no model is selected (ID=0) or invalid, default to visModel
    if (!targetModel) {
        targetModel = &resourceManager->getVisModel();
        std::cout << "[App] No model selected, defaulting to visModel" << std::endl;
    } else {
        std::cout << "[App] Remeshing model ID: " << selectedModelID << std::endl;
    }
    
    resourceManager->setSelectedModel(targetModel);  
    resourceManager->performRemeshing(targetModel, iterations, minAngleDegrees, maxEdgeLength, stepSize, *renderCommandPool, MAXFRAMESINFLIGHT);
    
    // Update descriptor sets for each remeshed model
    iODT* remesher = resourceManager->getRemesherForModel(targetModel);
    if (remesher && uniformBufferManager) {
        sceneRenderer->updateDescriptorSetsForModel(targetModel, remesher, *uniformBufferManager, MAXFRAMESINFLIGHT);
        sceneRenderer->updateNormalsDescriptorSetsForModel(targetModel, remesher, *uniformBufferManager, MAXFRAMESINFLIGHT);
        sceneRenderer->updateVertexNormalsDescriptorSetsForModel(targetModel, remesher, *uniformBufferManager, MAXFRAMESINFLIGHT);
    }
        
    // Recreate HeatSystem
    if (heatSystem) {
        heatSystem->cleanup();
        heatSystem->cleanupResources();
        heatSystem.reset();
        
        heatSystem = std::make_unique<HeatSystem>(
            vulkanDevice,
            *memoryAllocator,
            *resourceManager,
            *uniformBufferManager,
            MAXFRAMESINFLIGHT,
            *renderCommandPool,
            swapChainExtent,
            frameGraph->getRenderPass()
        );
        std::cout << "[App] HeatSystem recreated after remeshing" << std::endl;
    }
    
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
            MAXFRAMESINFLIGHT,
            *renderCommandPool,
            swapChainExtent,
            frameGraph->getRenderPass()
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
    uiCommandPool = std::make_unique<CommandPool>(vulkanDevice, "UI Thread Pool");
    renderCommandPool = std::make_unique<CommandPool>(vulkanDevice, "Render Thread Pool");
}

void App::initSwapChain() {
    createSwapChain();
    createImageViews();
}

void App::initRenderResources() {
    // Create frameGraph first since it owns render pass and image views
    frameGraph = std::make_unique<FrameGraph>(
            vulkanDevice,
            swapChainImageFormat,
            swapChainExtent,
            MAXFRAMESINFLIGHT
        );

        registerDefaultFrameGraphDesc(*frameGraph);
        frameGraph->createRenderPass(vulkanDevice, swapChainImageFormat);
        frameGraph->createImageViews(vulkanDevice, swapChainImageFormat, swapChainExtent, MAXFRAMESINFLIGHT);

        auto renderPass = frameGraph->getRenderPass();

        // Create UniformBufferManager
        uniformBufferManager = std::make_unique<UniformBufferManager>(
            vulkanDevice,
            *memoryAllocator,
            camera,
            MAXFRAMESINFLIGHT
        );

        // Create MaterialSystem (CPU-side material state and default CAD material)
        materialSystem = std::make_unique<MaterialSystem>(*uniformBufferManager);
        
        // Create LightingSystem (CPU-side lighting state)
        lightingSystem = std::make_unique<LightingSystem>(camera, *uniformBufferManager);

        // Create ResourceManager 
        resourceManager = std::make_unique<ResourceManager>(
            vulkanDevice,
            *memoryAllocator,
            *uniformBufferManager,
            renderPass,
            camera,
            MAXFRAMESINFLIGHT,
            frameGraph->getSubpassIndex("OverlayPass"),
            uiCommandPool.get(),      
            renderCommandPool.get()  
        );

        resourceManager->initialize();
        
        // Create ModelSelection for GPU based selection
        modelSelection = std::make_unique<ModelSelection>(vulkanDevice, *frameGraph);

        // Create heat system
        heatSystem = std::make_unique<HeatSystem>(
            vulkanDevice,
            *memoryAllocator,
            *resourceManager,
            *uniformBufferManager,
            MAXFRAMESINFLIGHT,
            *renderCommandPool,
            swapChainExtent,
            renderPass
        );

        // Create Gizmo for model transformation
        gizmo = std::make_unique<Gizmo>(
            vulkanDevice,
            *memoryAllocator,
            camera,
            renderPass,
            swapChainExtent,
            *renderCommandPool  
        );

        // Create SceneRenderer 
        sceneRenderer = std::make_unique<SceneRenderer>(
            vulkanDevice,
            *frameGraph,
            *resourceManager,
            *uniformBufferManager,
            WIDTH, HEIGHT,
            swapChainExtent,
            swapChainImageViews,
            swapChainImageFormat,
            MAXFRAMESINFLIGHT,
            *renderCommandPool,  
            false  // drawWireframe - unused, controlled by wireframeMode now
        );
        createComputeTimingQueryPool();
        frameGraph->createFramebuffers(swapChainImageViews, swapChainExtent, MAXFRAMESINFLIGHT);
        
        // Create wireframe renderer
        wireframeRenderer = std::make_unique<WireframeRenderer>(
            vulkanDevice, 
            sceneRenderer->getGbufferDescriptorSetLayout(),
            renderPass,
            2  // Overlay subpass (grid/points layer)
        );

        center = resourceManager->getVisModel().getBoundingBoxCenter();
        camera.setLookAt(center);

        // Create input manager
        inputManager = std::make_unique<InputManager>(
            camera, 
            *gizmo, 
            *modelSelection, 
            *resourceManager, 
            swapChainExtent, 
            *window
        );
        
        // Callbacks
        inputManager->onWireframeToggled = [this]() { 
            // Cycle through wireframe modes
            wireframeMode = static_cast<WireframeMode>((static_cast<int>(wireframeMode) + 1) % 3);
        };
        inputManager->onIntrinsicOverlayToggled = [this]() { intrinsicOverlayEnabled = !intrinsicOverlayEnabled; };
        inputManager->onHeatOverlayToggled = [this]() { heatOverlayEnabled = !heatOverlayEnabled; };
        inputManager->onTimingOverlayToggled = [this]() {
            gpuTimingOverlayEnabled = !gpuTimingOverlayEnabled;
            if (!gpuTimingOverlayEnabled && resourceManager) {
                resourceManager->updateTimingOverlayText({});
            }
        };
        inputManager->onToggleHeatSystem = [this]() { toggleHeatSystem(); };
        inputManager->onPauseHeatSystem = [this]() { pauseHeatSystem(); };
        inputManager->onResetHeatSystem = [this]() { resetHeatSystem(); };  
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
            
            // Poll input
            double x, y;
            window->getMousePosition(x, y);
            
            bool middlePressed = window->isMiddleButtonPressed();
            bool shiftPressed = window->isKeyPressed(Qt::Key_Shift);
            
            if (inputManager) {
                inputManager->processInput(
                    shiftPressed,
                    middlePressed,
                    x, y,
                    deltaTime
                );
            }
            
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
            
            // Update Gizmo interaction
            if (inputManager) {
                inputManager->updateGizmo();
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

    frameGraph->cleanupImages(vulkanDevice, MAXFRAMESINFLIGHT);

    sceneRenderer->freeCommandBuffers();

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(vulkanDevice.getDevice(), imageView, nullptr);
        }
        swapChainImageViews.clear();
        swapChainImages.clear();
        vkDestroySwapchainKHR(vulkanDevice.getDevice(), swapChain, nullptr);
    }

void App::recreateSwapChain() {
        std::cout << "[App] recreateSwapChain() called" << std::endl;
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

        // Clean up sync objects
        for (size_t i = 0; i < MAXFRAMESINFLIGHT; i++) {
            vkDestroySemaphore(vulkanDevice.getDevice(), renderFinishedSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vulkanDevice.getDevice(), computeFinishedSemaphores[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), inFlightFences[i], nullptr);
            vkDestroyFence(vulkanDevice.getDevice(), computeInFlightFences[i], nullptr);
        }

        cleanupSwapChain();
        
        // Recreate command buffers
        sceneRenderer->createCommandBuffers(MAXFRAMESINFLIGHT);

        createSwapChain();
        createImageViews();

        heatSystem->recreateResources(MAXFRAMESINFLIGHT, swapChainExtent, frameGraph->getRenderPass());

        frameGraph->createImageViews(vulkanDevice, swapChainImageFormat, swapChainExtent, MAXFRAMESINFLIGHT);
        frameGraph->createFramebuffers(swapChainImageViews, swapChainExtent, MAXFRAMESINFLIGHT);
        sceneRenderer->updateDescriptorSets(MAXFRAMESINFLIGHT);
        sceneRenderer->resize(swapChainExtent);

        createSyncObjects();

        currentFrame = 0;
    }

void App::cleanupRenderResources() {
        destroyComputeTimingQueryPool();
        modelSelection->cleanup();
        frameGraph->cleanup(vulkanDevice);
        sceneRenderer->cleanup(MAXFRAMESINFLIGHT);
        uniformBufferManager->cleanup(MAXFRAMESINFLIGHT);      
        heatSystem->cleanupResources();
        heatSystem->cleanup();
        if (gizmo) {
            gizmo->cleanup();
        }
        if (wireframeRenderer) {
            wireframeRenderer->cleanup();
        }
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
        if (uiCommandPool) {
            uiCommandPool.reset();
        }
        if (renderCommandPool) {
            renderCommandPool.reset();
        }
        
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
        if (isShuttingDown) {
            return;
        }
        
        SwapChainSupportDetails swapChainSupport = vulkanDevice.querySwapChainSupport(vulkanDevice.getPhysicalDevice(), vulkanDevice.getSurface());

        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
        
        // Validate extent is within bounds 
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

        // Always use identity transform 
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

void App::createComputeTimingQueryPool() {
        destroyComputeTimingQueryPool();

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(vulkanDevice.getPhysicalDevice(), &properties);
        computeTimestampPeriod = properties.limits.timestampPeriod;
        computeTimingValidFrames.assign(MAXFRAMESINFLIGHT, 0);

        if (computeTimestampPeriod <= 0.0f || MAXFRAMESINFLIGHT == 0) {
            return;
        }

        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = MAXFRAMESINFLIGHT * 2;

        if (vkCreateQueryPool(vulkanDevice.getDevice(), &queryInfo, nullptr, &computeTimingQueryPool) != VK_SUCCESS) {
            computeTimingQueryPool = VK_NULL_HANDLE;
            computeTimestampPeriod = 0.0f;
            return;
        }
    }

void App::destroyComputeTimingQueryPool() {
        computeTimingValidFrames.clear();
        computeTimestampPeriod = 0.0f;
        if (computeTimingQueryPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(vulkanDevice.getDevice(), computeTimingQueryPool, nullptr);
            computeTimingQueryPool = VK_NULL_HANDLE;
        }
    }

bool App::tryGetComputeGpuTimeMs(uint32_t frameIndex, float& outGpuMs) const {
        outGpuMs = 0.0f;
        if (computeTimingQueryPool == VK_NULL_HANDLE ||
            computeTimestampPeriod <= 0.0f ||
            frameIndex >= MAXFRAMESINFLIGHT ||
            frameIndex >= computeTimingValidFrames.size() ||
            computeTimingValidFrames[frameIndex] == 0) {
            return false;
        }

        uint64_t timestamps[2] = {};
        const VkResult result = vkGetQueryPoolResults(
            vulkanDevice.getDevice(),
            computeTimingQueryPool,
            frameIndex * 2,
            2,
            sizeof(timestamps),
            timestamps,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT);

        if (result != VK_SUCCESS || timestamps[1] <= timestamps[0]) {
            return false;
        }

        outGpuMs = static_cast<float>(timestamps[1] - timestamps[0]) * computeTimestampPeriod * 1e-6f;
        return true;
    }

void App::drawFrame() {
        // Skip rendering if shutting down or operating
        if (isShuttingDown || isOperating.load(std::memory_order_acquire)) {
            return;
        }
        
        // Wait for previous frame's fence
        vkWaitForFences(vulkanDevice.getDevice(), 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

        GpuTimingStats gpuTiming{};
        const bool hasGraphicsGpuTiming = sceneRenderer && sceneRenderer->tryGetGpuTimingStats(currentFrame, gpuTiming);
        float computeGpuMs = 0.0f;
        const bool hasComputeGpuTiming = tryGetComputeGpuTimeMs(currentFrame, computeGpuMs);

        const auto fpsNow = std::chrono::high_resolution_clock::now();
        if (!overlayFpsInitialized) {
            overlayFpsInitialized = true;
            overlayFpsSampleStart = fpsNow;
            overlayFpsFrameCount = 0;
            overlayFps = 0.0f;
        }
        ++overlayFpsFrameCount;
        const double fpsElapsed = std::chrono::duration<double>(fpsNow - overlayFpsSampleStart).count();
        if (fpsElapsed >= 0.25) {
            overlayFps = static_cast<float>(overlayFpsFrameCount / fpsElapsed);
            overlayFpsFrameCount = 0;
            overlayFpsSampleStart = fpsNow;
        }

        std::ostringstream fpsLine;
        const float frameTimeMs = (overlayFps > 0.001f) ? (1000.0f / overlayFps) : 0.0f;
        const uint32_t frameTimeMsRounded = static_cast<uint32_t>(frameTimeMs + 0.5f);
        fpsLine << std::fixed << std::setprecision(1) << "FPS: " << overlayFps
                << " (" << frameTimeMsRounded << " ms)";

        std::ostringstream gpuTotalLine;
        if (hasGraphicsGpuTiming && hasComputeGpuTiming) {
            gpuTotalLine << std::fixed << std::setprecision(2) << "GPU TOTAL: " << (gpuTiming.totalMs + computeGpuMs) << " ms";
        }
        else if (hasGraphicsGpuTiming && !hasComputeGpuTiming) {
            gpuTotalLine << std::fixed << std::setprecision(2) << "GPU TOTAL: " << gpuTiming.totalMs << " ms";
        }
        else {
            gpuTotalLine << "GPU TOTAL: -- ms";
        }

        std::ostringstream gpuGraphicsLine;
        if (hasGraphicsGpuTiming) {
            gpuGraphicsLine << std::fixed << std::setprecision(2) << "GPU GRAPHICS: " << gpuTiming.totalMs << " ms";
        }
        else {
            gpuGraphicsLine << "GPU GRAPHICS: -- ms";
        }

        std::ostringstream gpuComputeLine;
        if (hasComputeGpuTiming) {
            gpuComputeLine << std::fixed << std::setprecision(2) << "GPU COMPUTE: " << computeGpuMs << " ms";
        }
        else {
            gpuComputeLine << "GPU COMPUTE: -- ms";
        }

        std::vector<std::string> timingLines;

        if (hasGraphicsGpuTiming) {
            for (const GpuPassTiming& passTiming : gpuTiming.passTimings) {
                std::ostringstream passLine;
                passLine << std::fixed << std::setprecision(2) << passTiming.name << ": " << passTiming.ms << " ms";
                timingLines.push_back(passLine.str());
            }
        }

        timingLines.push_back(gpuComputeLine.str());
        timingLines.push_back(gpuGraphicsLine.str());
        timingLines.push_back(gpuTotalLine.str());
        timingLines.push_back(fpsLine.str());

        // Process pick results from previous frame 
        if (modelSelection) {
            modelSelection->processPickingRequests(currentFrame);
        }

        // Get next image
        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(vulkanDevice.getDevice(), swapChain, UINT64_MAX,
            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            std::cout << "[App::drawFrame] vkAcquireNextImageKHR returned " 
                      << (result == VK_ERROR_OUT_OF_DATE_KHR ? "VK_ERROR_OUT_OF_DATE_KHR" : "VK_SUBOPTIMAL_KHR") 
                      << " - triggering swapchain recreation" << std::endl;
            recreateSwapChain();
            return;
        }
        else if (result != VK_SUCCESS) {
            std::cout << "[App::drawFrame] vkAcquireNextImageKHR FAILED with result=" << result << std::endl;
            throw std::runtime_error("Failed to acquire swap chain image");
        }

        VkCommandBuffer commandBuffer = sceneRenderer->getCommandBuffers()[currentFrame];
        vkResetCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        vkResetFences(vulkanDevice.getDevice(), 1, &inFlightFences[currentFrame]);

        // Update gizmo interaction
        if (inputManager) {
            inputManager->updateGizmo();
        }
        
        // Process model selection requests
        UniformBufferObject ubo{};
        uniformBufferManager->updateUniformBuffer(swapChainExtent, currentFrame, ubo);

        GridUniformBufferObject gridUbo{};
        glm::vec3 gridSize = resourceManager->calculateMaxBoundingBoxSize();
        uniformBufferManager->updateGridUniformBuffer(currentFrame, ubo, gridUbo, gridSize);
        resourceManager->getGrid().updateLabels(gridSize);

        if (lightingSystem) {
            lightingSystem->update(currentFrame);
        }
        if (materialSystem) {
            materialSystem->update(currentFrame);
        }

        // Get arrow key state for heat source control
        bool upPressed = window->isKeyPressed(Qt::Key_Up);
        bool downPressed = window->isKeyPressed(Qt::Key_Down);
        bool leftPressed = window->isKeyPressed(Qt::Key_Left);
        bool rightPressed = window->isKeyPressed(Qt::Key_Right);
        
        heatSystem->processResetRequest();
        heatSystem->update(upPressed, downPressed, leftPressed, rightPressed, ubo, WIDTH, HEIGHT);
        const bool hasComputeWritesForGraphics = heatSystem->hasDispatchableComputeWork();
        const bool queuesAreShared = vulkanDevice.getComputeQueue() == vulkanDevice.getGraphicsQueue();
        bool computeSubmittedThisFrame = false;
        if (hasComputeWritesForGraphics) {
            if (currentFrame < computeTimingValidFrames.size()) {
                computeTimingValidFrames[currentFrame] = 0;
            }
            VkCommandBuffer computeCommandBuffer = heatSystem->getComputeCommandBuffers()[currentFrame];
            vkResetCommandBuffer(computeCommandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            vkResetFences(vulkanDevice.getDevice(), 1, &computeInFlightFences[currentFrame]);
            heatSystem->recordComputeCommands(
                computeCommandBuffer,
                currentFrame,
                computeTimingQueryPool,
                currentFrame * 2);

            VkSubmitInfo computeSubmitInfo{};
            computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            computeSubmitInfo.commandBufferCount = 1;
            computeSubmitInfo.pCommandBuffers = &computeCommandBuffer;
            if (!queuesAreShared) {
                computeSubmitInfo.signalSemaphoreCount = 1;
                computeSubmitInfo.pSignalSemaphores = &computeFinishedSemaphores[currentFrame];
            }

            const VkResult computeSubmitResult = vkQueueSubmit(vulkanDevice.getComputeQueue(), 1, &computeSubmitInfo, computeInFlightFences[currentFrame]);
            if (computeSubmitResult != VK_SUCCESS) {
                std::cout << "[App::drawFrame] compute vkQueueSubmit FAILED with result=" << computeSubmitResult;
                if (computeSubmitResult == VK_ERROR_DEVICE_LOST) {
                    std::cout << " (VK_ERROR_DEVICE_LOST)";
                }
                std::cout << " - triggering swapchain recreation" << std::endl;
                vkDeviceWaitIdle(vulkanDevice.getDevice());
                recreateSwapChain();
                return;
            }

            computeSubmittedThisFrame = true;
            if (currentFrame < computeTimingValidFrames.size()) {
                computeTimingValidFrames[currentFrame] = 1;
            }
        }

        frameGraph->setComputeSyncEnabled(hasComputeWritesForGraphics);

        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;
        waitSemaphores.reserve(2);
        waitStages.reserve(2);

        const bool graphicsWaitsForCompute = computeSubmittedThisFrame && !queuesAreShared;
        if (graphicsWaitsForCompute) {
            waitSemaphores.push_back(computeFinishedSemaphores[currentFrame]);
            waitStages.push_back(frameGraph->getComputeWaitDstStageMask());
        }

        waitSemaphores.push_back(imageAvailableSemaphores[currentFrame]);
        waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        if (gpuTimingOverlayEnabled) {
            for (std::string& line : timingLines) {
                std::transform(line.begin(), line.end(), line.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            }
            resourceManager->updateTimingOverlayText(timingLines);
        } else {
            resourceManager->updateTimingOverlayText({});
        }

        // Graphics pass 
        sceneRenderer->recordCommandBuffer(*resourceManager, *heatSystem, *modelSelection, *gizmo, *wireframeRenderer, swapChainImageViews, currentFrame, imageIndex, MAXFRAMESINFLIGHT, swapChainExtent, static_cast<int>(wireframeMode), intrinsicOverlayEnabled, heatOverlayEnabled, intrinsicNormalsEnabled, intrinsicVertexNormalsEnabled, intrinsicNormalLength, surfelsEnabled, voronoiEnabled, pointsEnabled, contactLinesEnabled);

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
            std::cout << "[App::drawFrame] vkQueueSubmit FAILED with result=" << result;
            if (result == VK_ERROR_DEVICE_LOST) {
                std::cout << " (VK_ERROR_DEVICE_LOST)";
            }
            std::cout << " - triggering swapchain recreation" << std::endl;
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
            std::cout << "[App::drawFrame] vkQueuePresentKHR returned " << result;
            if (result == VK_ERROR_OUT_OF_DATE_KHR) {
                std::cout << " (VK_ERROR_OUT_OF_DATE_KHR)";
            } else if (result == VK_SUBOPTIMAL_KHR) {
                std::cout << " (VK_SUBOPTIMAL_KHR)";
            } else if (result == VK_ERROR_DEVICE_LOST) {
                std::cout << " (VK_ERROR_DEVICE_LOST)";
            }
            std::cout << " - triggering swapchain recreation" << std::endl;
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
            // Surface is invalid/destroyed, return dummy extent
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
    // Debug printf goes to stdout, validation messages to stderr
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        std::cout << pCallbackData->pMessage << std::endl;  // Print debug printf to stdout
    }
    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

