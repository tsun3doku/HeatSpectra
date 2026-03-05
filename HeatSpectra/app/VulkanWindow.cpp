#include "VulkanWindow.hpp"

#include "App.h"
#include "AppTypes.hpp"

#include <QEvent>
#include <QExposeEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QVersionNumber>
#include <QVulkanInstance>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

namespace {
constexpr std::array<const char*, 1> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
}

VulkanWindow::VulkanWindow(QWindow* parent)
    : QWindow(parent) {
    setSurfaceType(QSurface::VulkanSurface);
}

VulkanWindow::~VulkanWindow() {
    cleanupVulkan();
}

void VulkanWindow::setApp(App* appPtr) {
    if (app == appPtr) {
        return;
    }

    if (app && appInitialized) {
        app->shutdown();
    }

    app = appPtr;
    appInitialized = false;
    frameTimer.invalidate();

    if (isExposed()) {
        requestRender();
    }
}

uint32_t VulkanWindow::physicalWidth() const {
    const qreal dpr = devicePixelRatio();
    return std::max(1u, static_cast<uint32_t>(std::max(1.0, width() * dpr)));
}

uint32_t VulkanWindow::physicalHeight() const {
    const qreal dpr = devicePixelRatio();
    return std::max(1u, static_cast<uint32_t>(std::max(1.0, height() * dpr)));
}

void VulkanWindow::exposeEvent(QExposeEvent* event) {
    QWindow::exposeEvent(event);
    if (isExposed()) {
        runtimeState.width.store(physicalWidth(), std::memory_order_release);
        runtimeState.height.store(physicalHeight(), std::memory_order_release);
        requestRender();
    }
}

bool VulkanWindow::event(QEvent* event) {
    if (event && event->type() == QEvent::UpdateRequest) {
        renderFrame();
        return true;
    }
    return QWindow::event(event);
}

void VulkanWindow::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
    runtimeState.width.store(physicalWidth(), std::memory_order_release);
    runtimeState.height.store(physicalHeight(), std::memory_order_release);
    if (isExposed()) {
        requestRender();
    }
}

void VulkanWindow::keyPressEvent(QKeyEvent* event) {
    if (!event || event->isAutoRepeat()) {
        QWindow::keyPressEvent(event);
        return;
    }

    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (key == Qt::Key_Shift) {
        runtimeState.shiftPressed.store(true, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Key;
    inputEvent.key = static_cast<int>(key);
    inputEvent.pressed = true;
    runtimeState.pushInputEvent(inputEvent);
    QWindow::keyPressEvent(event);
}

void VulkanWindow::keyReleaseEvent(QKeyEvent* event) {
    if (!event || event->isAutoRepeat()) {
        QWindow::keyReleaseEvent(event);
        return;
    }

    const Qt::Key key = static_cast<Qt::Key>(event->key());
    if (key == Qt::Key_Shift) {
        runtimeState.shiftPressed.store(false, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Key;
    inputEvent.key = static_cast<int>(key);
    inputEvent.pressed = false;
    runtimeState.pushInputEvent(inputEvent);
    QWindow::keyReleaseEvent(event);
}

void VulkanWindow::mousePressEvent(QMouseEvent* event) {
    if (!event) {
        QWindow::mousePressEvent(event);
        return;
    }

    setRuntimeMousePosition(event->position());

    if (event->button() == Qt::MiddleButton) {
        runtimeState.middleButtonPressed.store(true, std::memory_order_release);
    }

    if (event->button() == Qt::LeftButton) {
        WindowInputEvent inputEvent{};
        inputEvent.type = WindowInputEventType::MousePress;
        inputEvent.button = static_cast<int>(event->button());
        inputEvent.x = runtimeState.mouseX.load(std::memory_order_acquire);
        inputEvent.y = runtimeState.mouseY.load(std::memory_order_acquire);
        inputEvent.shiftPressed = (event->modifiers() & Qt::ShiftModifier) != 0;
        runtimeState.pushInputEvent(inputEvent);
    }

    QWindow::mousePressEvent(event);
}

void VulkanWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (!event) {
        QWindow::mouseReleaseEvent(event);
        return;
    }

    setRuntimeMousePosition(event->position());

    if (event->button() == Qt::MiddleButton) {
        runtimeState.middleButtonPressed.store(false, std::memory_order_release);
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseRelease;
    inputEvent.button = static_cast<int>(event->button());
    inputEvent.x = runtimeState.mouseX.load(std::memory_order_acquire);
    inputEvent.y = runtimeState.mouseY.load(std::memory_order_acquire);
    runtimeState.pushInputEvent(inputEvent);
    QWindow::mouseReleaseEvent(event);
}

void VulkanWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!event) {
        QWindow::mouseMoveEvent(event);
        return;
    }

    setRuntimeMousePosition(event->position());

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::MouseMove;
    inputEvent.x = runtimeState.mouseX.load(std::memory_order_acquire);
    inputEvent.y = runtimeState.mouseY.load(std::memory_order_acquire);
    runtimeState.pushInputEvent(inputEvent);
    QWindow::mouseMoveEvent(event);
}

void VulkanWindow::wheelEvent(QWheelEvent* event) {
    if (!event) {
        QWindow::wheelEvent(event);
        return;
    }

    WindowInputEvent inputEvent{};
    inputEvent.type = WindowInputEventType::Scroll;
    inputEvent.xOffset = event->angleDelta().x() / 120.0;
    inputEvent.yOffset = event->angleDelta().y() / 120.0;
    runtimeState.pushInputEvent(inputEvent);
    QWindow::wheelEvent(event);
}

void VulkanWindow::focusOutEvent(QFocusEvent* event) {
    runtimeState.shiftPressed.store(false, std::memory_order_release);
    runtimeState.middleButtonPressed.store(false, std::memory_order_release);
    QWindow::focusOutEvent(event);
}

void VulkanWindow::requestRender() {
    if (isExposed()) {
        requestUpdate();
    }
}

void VulkanWindow::renderFrame() {
    if (!isExposed()) {
        return;
    }

    if (!ensureInitialized()) {
        requestRender();
        return;
    }

    if (!app) {
        return;
    }

    const float deltaSeconds = frameTimer.isValid()
        ? static_cast<float>(frameTimer.restart()) / 1000.0f
        : (1.0f / 60.0f);
    app->tickFrame(deltaSeconds);

    requestRender();
}

bool VulkanWindow::ensureInitialized() {
    if (!vulkanInitialized && !initializeVulkan()) {
        return false;
    }
    if (!appInitialized && !initializeApp()) {
        return false;
    }
    return true;
}

bool VulkanWindow::initializeVulkan() {
    if (vulkanInitialized) {
        return true;
    }

    if (!qtVulkanInstance) {
        qtVulkanInstance = new QVulkanInstance();
        qtVulkanInstance->setApiVersion(QVersionNumber(1, 3));
        if (!qtVulkanInstance->create()) {
            delete qtVulkanInstance;
            qtVulkanInstance = nullptr;
            return false;
        }
        setVulkanInstance(qtVulkanInstance);
    }

    const VkSurfaceKHR surface = qtVulkanInstance->surfaceForWindow(this);
    if (surface == VK_NULL_HANDLE) {
        return false;
    }

    try {
        vulkanDevice.init(
            qtVulkanInstance->vkInstance(),
            surface,
            std::vector<const char*>(kDeviceExtensions.begin(), kDeviceExtensions.end()),
            {},
            false);
    } catch (const std::exception& ex) {
        std::cerr << "[VulkanWindow] Failed to initialize VulkanDevice: " << ex.what() << std::endl;
        vulkanDevice.cleanup();
        return false;
    }

    vulkanInitialized = true;
    return true;
}

bool VulkanWindow::initializeApp() {
    if (!app || appInitialized) {
        return appInitialized;
    }

    AppVulkanContext context{};
    context.physicalDevice = vulkanDevice.getPhysicalDevice();
    context.device = vulkanDevice.getDevice();
    context.graphicsQueue = vulkanDevice.getGraphicsQueue();
    context.queueFamilyIndex = vulkanDevice.getQueueFamilyIndices().graphicsAndComputeFamily.value_or(0);
    context.surface = vulkanDevice.getSurface();

    runtimeState.width.store(physicalWidth(), std::memory_order_release);
    runtimeState.height.store(physicalHeight(), std::memory_order_release);
    appInitialized = app->initialize(runtimeState, context);
    if (appInitialized) {
        frameTimer.start();
    }
    return appInitialized;
}

void VulkanWindow::cleanupVulkan() {
    runtimeState.shouldClose.store(true, std::memory_order_release);

    if (app && appInitialized) {
        app->shutdown();
    }
    appInitialized = false;

    if (vulkanDevice.getDevice() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkanDevice.getDevice());
    }

    vulkanDevice.cleanup();
    vulkanInitialized = false;

    if (qtVulkanInstance) {
        delete qtVulkanInstance;
        qtVulkanInstance = nullptr;
    }
}

void VulkanWindow::setRuntimeMousePosition(const QPointF& position) {
    const qreal dpr = devicePixelRatio();
    runtimeState.mouseX.store(static_cast<float>(position.x() * dpr), std::memory_order_release);
    runtimeState.mouseY.store(static_cast<float>(position.y() * dpr), std::memory_order_release);
}
