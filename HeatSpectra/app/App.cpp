#include "App.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "runtime/RuntimeSystems.hpp"

App::App()
    : systems(std::make_unique<RuntimeSystems>()) {
}

App::~App() {
    shutdown();
}

bool App::initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext) {
    return systems && systems->initialize(runtimeState, vulkanContext);
}

void App::tickFrame(float deltaTime) {
    if (systems) {
        systems->tickFrame(deltaTime);
    }
}

void App::shutdown() {
    if (systems) {
        systems->shutdown();
    }
}

bool App::isInitialized() const {
    return systems && systems->isInitialized();
}

const RuntimeQuery* App::runtimeQuery() const {
    if (systems) {
        return systems->runtimeQuery();
    }
    return nullptr;
}

std::vector<SimulationError> App::consumeSimulationErrors() {
    if (systems) {
        return systems->consumeSimulationErrors();
    }
    return {};
}

void App::setPanSensitivity(float sensitivity) {
    if (systems) {
        systems->setPanSensitivity(sensitivity);
    }
}

RenderSettingsController* App::getSettingsController() {
    if (systems) {
        return systems->getSettingsController();
    }
    return nullptr;
}

const RenderSettingsController* App::getSettingsController() const {
    if (systems) {
        return systems->getSettingsController();
    }
    return nullptr;
}

NodeGraphBridge* App::getNodeGraphBridge() {
    if (systems) {
        return systems->getNodeGraphBridge();
    }
    return nullptr;
}

const NodeGraphBridge* App::getNodeGraphBridge() const {
    if (systems) {
        return systems->getNodeGraphBridge();
    }
    return nullptr;
}

SceneController* App::getSceneController() {
    if (systems) {
        return systems->getSceneController();
    }
    return nullptr;
}

const SceneController* App::getSceneController() const {
    if (systems) {
        return systems->getSceneController();
    }
    return nullptr;
}

ModelSelection* App::getModelSelection() {
    if (systems) {
        return systems->getModelSelection();
    }
    return nullptr;
}

const ModelSelection* App::getModelSelection() const {
    if (systems) {
        return systems->getModelSelection();
    }
    return nullptr;
}

void App::setRenderPaused(bool paused) {
    if (systems) {
        systems->setRenderPaused(paused);
    }
}

uint32_t App::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    if (systems) {
        return systems->loadModel(modelPath, preferredModelId);
    }
    return 0;
}