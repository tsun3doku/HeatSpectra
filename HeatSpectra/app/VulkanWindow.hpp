#pragma once

#include <QElapsedTimer>
#include <QWindow>

#include <vulkan/vulkan.h>

#include "render/WindowRuntimeState.hpp"
#include "vulkan/VulkanDevice.hpp"

class QVulkanInstance;
class App;

class VulkanWindow : public QWindow {
public:
    explicit VulkanWindow(QWindow* parent = nullptr);
    ~VulkanWindow() override;

    void setApp(App* appPtr);

    uint32_t physicalWidth() const;
    uint32_t physicalHeight() const;

protected:
    void exposeEvent(QExposeEvent* event) override;
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void requestRender();
    void renderFrame();
    bool ensureInitialized();
    bool initializeVulkan();
    bool initializeApp();
    void cleanupVulkan();

    void setRuntimeMousePosition(const QPointF& position);

    App* app = nullptr;
    bool appInitialized = false;
    bool vulkanInitialized = false;

    QVulkanInstance* qtVulkanInstance = nullptr;
    VulkanDevice vulkanDevice;

    WindowRuntimeState runtimeState{};
    QElapsedTimer frameTimer;
};
