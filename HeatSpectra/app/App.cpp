#include "App.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "runtime/RuntimeHost.hpp"

App::App()
    : runtimeHost(std::make_unique<RuntimeHost>()) {
}

App::~App() {
    shutdown();
}

bool App::initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext) {
    return runtimeHost && runtimeHost->initialize(runtimeState, vulkanContext);
}

void App::tickFrame(float deltaTime) {
    if (runtimeHost) {
        runtimeHost->tickFrame(deltaTime);
    }
}

void App::shutdown() {
    if (runtimeHost) {
        runtimeHost->shutdown();
    }
}

bool App::isInitialized() const {
    return runtimeHost && runtimeHost->isInitialized();
}

AppViewportOutput App::getViewportOutput() const {
    if (runtimeHost) {
        return runtimeHost->getViewportOutput();
    }
    return {};
}

const RuntimeQuery* App::runtimeQuery() const {
    if (runtimeHost) {
        return runtimeHost->runtimeQuery();
    }
    return nullptr;
}

void App::setPanSensitivity(float sensitivity) {
    if (runtimeHost) {
        runtimeHost->setPanSensitivity(sensitivity);
    }
}

RenderSettingsController* App::getSettingsController() {
    if (runtimeHost) {
        return runtimeHost->getSettingsController();
    }
    return nullptr;
}

const RenderSettingsController* App::getSettingsController() const {
    if (runtimeHost) {
        return runtimeHost->getSettingsController();
    }
    return nullptr;
}

NodeGraphBridge* App::getNodeGraphBridge() {
    if (runtimeHost) {
        return runtimeHost->getNodeGraphBridge();
    }
    return nullptr;
}

const NodeGraphBridge* App::getNodeGraphBridge() const {
    if (runtimeHost) {
        return runtimeHost->getNodeGraphBridge();
    }
    return nullptr;
}

void App::setRenderPaused(bool paused) {
    if (runtimeHost) {
        runtimeHost->setRenderPaused(paused);
    }
}

uint32_t App::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    if (runtimeHost) {
        return runtimeHost->loadModel(modelPath, preferredModelId);
    }
    return 0;
}
