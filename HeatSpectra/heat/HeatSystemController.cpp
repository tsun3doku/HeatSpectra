#include "HeatSystemController.hpp"

#include "vulkan/CommandBufferManager.hpp"
#include "app/SwapchainManager.hpp"
#include "framegraph/FrameSync.hpp"
#include "HeatSystem.hpp"
#include "vulkan/MemoryAllocator.hpp"
#include "mesh/MeshModifiers.hpp"
#include "scene/Model.hpp"
#include "scene/ModelUploader.hpp"
#include "render/RenderConfig.hpp"
#include "vulkan/ResourceManager.hpp"
#include "render/SceneRenderer.hpp"
#include "vulkan/UniformBufferManager.hpp"
#include "framegraph/VkFrameGraphRuntime.hpp"
#include "vulkan/VulkanDevice.hpp"

#include <chrono>
#include <iostream>
#include <thread>
#include <unordered_set>

HeatSystemController::HeatSystemController(VulkanDevice& vulkanDevice, MemoryAllocator& memoryAllocator, ResourceManager& resourceManager,
    MeshModifiers& meshModifiers, ModelUploader& modelUploader, UniformBufferManager& uniformBufferManager, SceneRenderer& sceneRenderer,
    SwapchainManager& swapchainManager, VkFrameGraphRuntime& frameGraphRuntime, CommandPool& renderCommandPool, FrameSync& frameSync,
    std::unique_ptr<HeatSystem>& heatSystem, std::atomic<bool>& isOperating, uint32_t maxFramesInFlight)
    : vulkanDevice(vulkanDevice),
      memoryAllocator(memoryAllocator),
      resourceManager(resourceManager),
      meshModifiers(meshModifiers),
      modelUploader(modelUploader),
      uniformBufferManager(uniformBufferManager),
      sceneRenderer(sceneRenderer),
      swapchainManager(swapchainManager),
      frameGraphRuntime(frameGraphRuntime),
      renderCommandPool(renderCommandPool),
      frameSync(frameSync),
      heatSystem(heatSystem),
      isOperating(isOperating),
      maxFramesInFlight(maxFramesInFlight) {
}

bool HeatSystemController::isHeatSystemActive() const {
    return heatSystem && heatSystem->getIsActive();
}

bool HeatSystemController::isHeatSystemPaused() const {
    return heatSystem && heatSystem->getIsPaused();
}

FrameSimulation* HeatSystemController::getHeatSystem() const {
    return heatSystem.get();
}

std::unique_ptr<HeatSystem> HeatSystemController::buildHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    std::unique_ptr<HeatSystem> system = std::make_unique<HeatSystem>(
        vulkanDevice,
        memoryAllocator,
        resourceManager,
        meshModifiers.getRemesher(),
        uniformBufferManager,
        maxFramesInFlight,
        renderCommandPool,
        extent,
        renderPass);
    if (!system || !system->isInitialized()) {
        std::cerr << "[HeatSystemController] HeatSystem initialization failed" << std::endl;
        return nullptr;
    }
    system->setActiveModels(configuredSourceModelIds, configuredReceiverModelIds);
    system->setMaterialBindings(configuredMaterialBindings);
    return system;
}

void HeatSystemController::setActiveModels(
    const std::vector<uint32_t>& sourceModelIds,
    const std::vector<uint32_t>& receiverModelIds) {
    if (configuredSourceModelIds == sourceModelIds &&
        configuredReceiverModelIds == receiverModelIds) {
        return;
    }

    configuredSourceModelIds = sourceModelIds;
    configuredReceiverModelIds = receiverModelIds;
    if (heatSystem) {
        heatSystem->setActiveModels(configuredSourceModelIds, configuredReceiverModelIds);
    }
}

void HeatSystemController::setMaterialBindings(const std::vector<HeatModelMaterialBindings>& bindings) {
    configuredMaterialBindings = bindings;
    if (heatSystem) {
        heatSystem->setMaterialBindings(configuredMaterialBindings);
    }
}

void HeatSystemController::createHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    heatSystem = buildHeatSystem(extent, renderPass);
}

void HeatSystemController::recreateHeatSystem(VkExtent2D extent, VkRenderPass renderPass) {
    if (heatSystem) {
        heatSystem->cleanupResources();
        heatSystem->cleanup();
        heatSystem.reset();
    }

    heatSystem = buildHeatSystem(extent, renderPass);
}

void HeatSystemController::toggleHeatSystem() {
    if (!heatSystem) {
        return;
    }

    const bool isActive = heatSystem->getIsActive();
    const bool isPaused = heatSystem->getIsPaused();

    if (isActive && isPaused) {
        heatSystem->setIsPaused(false);
        return;
    }

    const bool newState = !isActive;

    Remesher& remesher = meshModifiers.getRemesher();
    std::unordered_set<uint32_t> requiredModelIds;
    for (uint32_t modelId : configuredSourceModelIds) {
        if (modelId != 0) {
            requiredModelIds.insert(modelId);
        }
    }
    for (uint32_t modelId : configuredReceiverModelIds) {
        if (modelId != 0) {
            requiredModelIds.insert(modelId);
        }
    }

    if (newState) {
        if (requiredModelIds.empty()) {
            std::cerr << "[HeatSystemController] Cannot activate heatsystem: no source/receiver models are configured" << std::endl;
            return;
        }

        Remesher& activeRemesher = meshModifiers.getRemesher();
        for (uint32_t modelId : requiredModelIds) {
            Model* model = resourceManager.getModelByID(modelId);
            if (!model || !activeRemesher.isModelRemeshed(model)) {
                std::cerr << "[HeatSystemController] Cannot activate heatsystem: model "
                          << modelId << " is not remeshed" << std::endl;
                return;
            }
        }
    }

    if (newState && !heatSystem->getIsVoronoiReady()) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
        isOperating.store(true, std::memory_order_release);
    }

    heatSystem->setActive(newState);
    heatSystem->setIsPaused(false);

    if (newState) {
        for (uint32_t modelId : requiredModelIds) {
            Model* model = resourceManager.getModelByID(modelId);
            if (!model) {
                continue;
            }

            auto* modelRemesher = remesher.getRemesherForModel(model);
            if (!modelRemesher) {
                continue;
            }

            sceneRenderer.updateDescriptorSetsForModel(model, modelRemesher);
            sceneRenderer.updateNormalsDescriptorSetsForModel(model, modelRemesher);
            sceneRenderer.updateVertexNormalsDescriptorSetsForModel(model, modelRemesher);
        }
    }

    if (!newState) {
        heatSystem->requestReset();
    }

    if (newState && isOperating.load(std::memory_order_acquire)) {
        isOperating.store(false, std::memory_order_release);
    }
}

void HeatSystemController::pauseHeatSystem() {
    if (heatSystem && heatSystem->getIsActive() && !heatSystem->getIsPaused()) {
        heatSystem->setIsPaused(true);
    }
}

void HeatSystemController::resetHeatSystem() {
    if (!heatSystem) {
        return;
    }

    std::unordered_set<uint32_t> requiredModelIds;
    for (uint32_t modelId : configuredSourceModelIds) {
        if (modelId != 0) {
            requiredModelIds.insert(modelId);
        }
    }
    for (uint32_t modelId : configuredReceiverModelIds) {
        if (modelId != 0) {
            requiredModelIds.insert(modelId);
        }
    }

    if (requiredModelIds.empty()) {
        std::cerr << "[HeatSystemController] Cannot reset/reactivate heat system: no source/receiver models are configured" << std::endl;
        return;
    }

    Remesher& activeRemesher = meshModifiers.getRemesher();
    for (uint32_t modelId : requiredModelIds) {
        Model* model = resourceManager.getModelByID(modelId);
        if (!model || !activeRemesher.isModelRemeshed(model)) {
            std::cerr << "[HeatSystemController] Cannot reset/reactivate heat system: model "
                      << modelId << " is not remeshed" << std::endl;
            return;
        }
    }

    heatSystem->requestReset();
    heatSystem->setIsPaused(false);
}

uint32_t HeatSystemController::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    std::cout << "[HeatSystemController] Adding model: " << modelPath << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    if (preferredModelId != 0) {
        if (Model* existingModel = resourceManager.getModelByID(preferredModelId)) {
            meshModifiers.getRemesher().removeModel(existingModel);
        }
        resourceManager.removeModelByID(preferredModelId);
    }

    const uint32_t modelId = modelUploader.addModel(resourceManager, modelPath, preferredModelId);

    if (heatSystem) {
        recreateHeatSystem(swapchainManager.getExtent(), frameGraphRuntime.getRenderPass());
    }

    std::cout << "[HeatSystemController] Added model with runtime ID: " << modelId << std::endl;
    return modelId;
}

bool HeatSystemController::removeModelByID(uint32_t modelId) {
    if (modelId == 0) {
        return false;
    }

    std::cout << "[HeatSystemController] Removing model ID: " << modelId << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(renderconfig::ModelLoadPauseMs));

    frameSync.waitForAllFrameFences();
    vkDeviceWaitIdle(vulkanDevice.getDevice());

    if (Model* existingModel = resourceManager.getModelByID(modelId)) {
        meshModifiers.getRemesher().removeModel(existingModel);
    }

    const bool removed = resourceManager.removeModelByID(modelId);
    if (removed && heatSystem) {
        recreateHeatSystem(swapchainManager.getExtent(), frameGraphRuntime.getRenderPass());
    }

    if (removed) {
        std::cout << "[HeatSystemController] Removed model ID: " << modelId << std::endl;
    }
    return removed;
}

