#include "VulkanWindow.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>

#ifdef _WIN32
#include <Windows.h>
#endif

VulkanWindow::VulkanWindow(QWindow* parent) : QWindow(parent) {
    setSurfaceType(QSurface::VulkanSurface);
    setTitle("HeatSpectra");
    resize(960, 540);
    
    // Enable mouse tracking
    setMouseGrabEnabled(false);
}

VulkanWindow::~VulkanWindow() = default;

void* VulkanWindow::getNativeWindowHandle() {
#ifdef _WIN32
    return reinterpret_cast<void*>(winId());
#else
    return reinterpret_cast<void*>(winId());
#endif
}

HINSTANCE VulkanWindow::getNativeInstance() {
#ifdef _WIN32
    return GetModuleHandle(nullptr);
#else
    return nullptr;
#endif
}

bool VulkanWindow::isKeyPressed(Qt::Key key) const {
    QMutexLocker locker(&mutex);
    return pressedKeys.contains(key);
}

void VulkanWindow::getMousePosition(double& x, double& y) const {
    QMutexLocker locker(&mutex);
    x = mousePos.x();
    y = mousePos.y();
}

bool VulkanWindow::isMiddleButtonPressed() const {
    QMutexLocker locker(&mutex);
    return middleButtonPressed;
}

void VulkanWindow::setScrollCallback(std::function<void(double, double)> callback) {
    scrollCb = callback;
}

void VulkanWindow::setKeyCallback(std::function<void(Qt::Key, bool)> callback) {
    keyCb = callback;
}

void VulkanWindow::setMouseClickCallback(std::function<void(int, float, float, bool)> callback) {
    mouseClickCb = callback;
}

void VulkanWindow::setMouseMoveCallback(std::function<void(float, float)> callback) {
    mouseMoveCb = callback;
}

void VulkanWindow::setMouseReleaseCallback(std::function<void(int, float, float)> callback) {
    mouseReleaseCb = callback;
}

void VulkanWindow::resizeEvent(QResizeEvent* event) {
    QWindow::resizeEvent(event);
}

void VulkanWindow::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        return;
    }
    
    Qt::Key key = static_cast<Qt::Key>(event->key());
    
    {
        QMutexLocker locker(&mutex);
        pressedKeys.insert(key);
    }
    
    if (keyCb) {
        keyCb(key, true);
    }
    
    QWindow::keyPressEvent(event);
}

void VulkanWindow::keyReleaseEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        return;
    }
    
    Qt::Key key = static_cast<Qt::Key>(event->key());
    
    {
        QMutexLocker locker(&mutex);
        pressedKeys.remove(key);
    }
    
    if (keyCb) {
        keyCb(key, false);
    }
    
    QWindow::keyReleaseEvent(event);
}

void VulkanWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        QMutexLocker locker(&mutex);
        middleButtonPressed = true;
    }
    
    // Trigger mouse click callback for left clicks
    if (event->button() == Qt::LeftButton && mouseClickCb) {
        float mouseX = event->position().x();
        float mouseY = event->position().y();
        bool shiftPressed = event->modifiers() & Qt::ShiftModifier;
        mouseClickCb(static_cast<int>(event->button()), mouseX, mouseY, shiftPressed);
    }
    
    QWindow::mousePressEvent(event);
}

void VulkanWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        QMutexLocker locker(&mutex);
        middleButtonPressed = false;
    }
    
    // Trigger mouse release callback
    if (mouseReleaseCb) {
        float mouseX = event->position().x();
        float mouseY = event->position().y();
        mouseReleaseCb(static_cast<int>(event->button()), mouseX, mouseY);
    }
    
    QWindow::mouseReleaseEvent(event);
}

void VulkanWindow::mouseMoveEvent(QMouseEvent* event) {
    {
        QMutexLocker locker(&mutex);
        mousePos = event->position();
    }
    
    // Trigger mouse move callback
    if (mouseMoveCb) {
        float mouseX = event->position().x();
        float mouseY = event->position().y();
        mouseMoveCb(mouseX, mouseY);
    }
    
    QWindow::mouseMoveEvent(event);
}

void VulkanWindow::wheelEvent(QWheelEvent* event) {
    if (scrollCb) {
        double xOffset = event->angleDelta().x() / 120.0;
        double yOffset = event->angleDelta().y() / 120.0;
        scrollCb(xOffset, yOffset);
    }
    
    QWindow::wheelEvent(event);
}
