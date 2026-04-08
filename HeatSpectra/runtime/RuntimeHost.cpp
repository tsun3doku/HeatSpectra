#include "RuntimeHost.hpp"

#include "RuntimeSystems.hpp"

RuntimeHost::RuntimeHost()
    : systems(std::make_unique<RuntimeSystems>()) {
}

RuntimeHost::~RuntimeHost() {
    shutdown();
}

bool RuntimeHost::initialize(WindowRuntimeState& runtimeState, const AppVulkanContext& vulkanContext) {
    return systems->initialize(runtimeState, vulkanContext);
}

void RuntimeHost::tickFrame(float deltaTime) {
    systems->tickFrame(deltaTime);
}

void RuntimeHost::shutdown() {
    systems->shutdown();
}

bool RuntimeHost::isInitialized() const {
    return systems->isInitialized();
}

const RuntimeQuery* RuntimeHost::runtimeQuery() const {
    return systems->runtimeQuery();
}

std::vector<SimulationError> RuntimeHost::consumeSimulationErrors() {
    return systems->consumeSimulationErrors();
}

uint32_t RuntimeHost::loadModel(const std::string& modelPath, uint32_t preferredModelId) {
    return systems->loadModel(modelPath, preferredModelId);
}

void RuntimeHost::setPanSensitivity(float sensitivity) {
    systems->setPanSensitivity(sensitivity);
}

void RuntimeHost::setRenderPaused(bool paused) {
    systems->setRenderPaused(paused);
}

RenderSettingsController* RuntimeHost::getSettingsController() {
    return systems->getSettingsController();
}

const RenderSettingsController* RuntimeHost::getSettingsController() const {
    return systems->getSettingsController();
}

NodeGraphBridge* RuntimeHost::getNodeGraphBridge() {
    return systems->getNodeGraphBridge();
}

const NodeGraphBridge* RuntimeHost::getNodeGraphBridge() const {
    return systems->getNodeGraphBridge();
}

SceneController* RuntimeHost::getSceneController() {
    return systems->getSceneController();
}

const SceneController* RuntimeHost::getSceneController() const {
    return systems->getSceneController();
}

ModelSelection* RuntimeHost::getModelSelection() {
    return systems->getModelSelection();
}

const ModelSelection* RuntimeHost::getModelSelection() const {
    return systems->getModelSelection();
}
