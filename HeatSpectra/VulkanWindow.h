#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

#include <QWindow>
#include <QTimer>
#include <QSet>
#include <QMutex>
#include <functional>
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
    void setScrollCallback(std::function<void(double, double)> callback);
    void setKeyCallback(std::function<void(Qt::Key, bool)> callback);
    void setMouseClickCallback(std::function<void(int, float, float, bool)> callback);
    void setMouseMoveCallback(std::function<void(float, float)> callback);
    void setMouseReleaseCallback(std::function<void(int, float, float)> callback);

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
    std::function<void(double, double)> scrollCb;
    std::function<void(Qt::Key, bool)> keyCb;
    std::function<void(int, float, float, bool)> mouseClickCb;
    std::function<void(float, float)> mouseMoveCb;
    std::function<void(int, float, float)> mouseReleaseCb;

    mutable QMutex mutex;
};
