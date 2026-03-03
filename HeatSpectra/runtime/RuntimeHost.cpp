#include "RuntimeHost.hpp"

#include "RuntimeSystems.hpp"

RuntimeHost::RuntimeHost()
    : systems(std::make_unique<RuntimeSystems>()) {
}

RuntimeHost::~RuntimeHost() {
    shutdown();
}

bool RuntimeHost::initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext) {
    return systems && systems->initialize(runtimeState, vulkanContext);
}

void RuntimeHost::tickFrame(float deltaTime) {
    if (systems) {
        systems->tickFrame(deltaTime);
    }
}

void RuntimeHost::shutdown() {
    if (systems) {
        systems->shutdown();
    }
}

bool RuntimeHost::isInitialized() const {
    return systems && systems->isInitialized();
}

AppViewportOutput RuntimeHost::getViewportOutput() const {
    if (systems) {
        return systems->getViewportOutput();
    }
    return {};
}

const RuntimeQuery* RuntimeHost::runtimeQuery() const {
    if (systems) {
        return systems->runtimeQuery();
    }
    return nullptr;
}

uint32_t RuntimeHost::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    if (systems) {
        return systems->loadModel(modelPath, preferredModelId);
    }
    return 0;
}

void RuntimeHost::setPanSensitivity(float sensitivity) {
    if (systems) {
        systems->setPanSensitivity(sensitivity);
    }
}

void RuntimeHost::setRenderPaused(bool paused) {
    if (systems) {
        systems->setRenderPaused(paused);
    }
}

RenderSettingsController* RuntimeHost::getSettingsController() {
    if (systems) {
        return systems->getSettingsController();
    }
    return nullptr;
}

const RenderSettingsController* RuntimeHost::getSettingsController() const {
    if (systems) {
        return systems->getSettingsController();
    }
    return nullptr;
}

NodeGraphBridge* RuntimeHost::getNodeGraphBridge() {
    if (systems) {
        return systems->getNodeGraphBridge();
    }
    return nullptr;
}

const NodeGraphBridge* RuntimeHost::getNodeGraphBridge() const {
    if (systems) {
        return systems->getNodeGraphBridge();
    }
    return nullptr;
}
