#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

#include <QWindow>
#include <QTimer>
#include <QSet>
#include <vulkan/vulkan.h>

class Camera;

class VulkanWindow : public QWindow {
    Q_OBJECT

public:
    VulkanWindow(QWindow* parent = nullptr);
    ~VulkanWindow();

    // Get native window handle for Vulkan surface creation
    void* getNativeWindowHandle();
    HINSTANCE getNativeInstance();
    
    // Input state 
    bool isKeyPressed(Qt::Key key) const;
    void getMousePosition(double& x, double& y) const;
    bool isMiddleButtonPressed() const;
    
    // Window state
    bool shouldClose() const { 
        return shouldCloseFlag; 
    }
    void setShouldClose(bool value) { 
        shouldCloseFlag = value; 
    }
    
    // Callbacks for your App class
    void setScrollCallback(void (*callback)(void*, double, double), void* userPtr);
    void setKeyCallback(void (*callback)(void*, Qt::Key, bool), void* userPtr);
    void setMouseClickCallback(void (*callback)(void*, int, float, float, bool), void* userPtr);
    void setMouseMoveCallback(void (*callback)(void*, float, float), void* userPtr);
    void setMouseReleaseCallback(void (*callback)(void*, int, float, float), void* userPtr);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    bool shouldCloseFlag = false;
    
    // Input state tracking
    QSet<Qt::Key> pressedKeys;
    bool middleButtonPressed = false;
    QPointF mousePos;
    
    // Callbacks
    void (*scrollCb)(void*, double, double) = nullptr;
    void* scrollUserPtr = nullptr;
    
    void (*keyCb)(void*, Qt::Key, bool) = nullptr;
    void* keyUserPtr = nullptr;
    
    void (*mouseClickCb)(void*, int, float, float, bool) = nullptr;
    void* mouseClickUserPtr = nullptr;
    
    void (*mouseMoveCb)(void*, float, float) = nullptr;
    void* mouseMoveUserPtr = nullptr;
    
    void (*mouseReleaseCb)(void*, int, float, float) = nullptr;
    void* mouseReleaseUserPtr = nullptr;
};
